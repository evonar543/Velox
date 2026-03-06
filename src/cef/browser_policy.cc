#include "cef/browser_policy.h"

#include <array>
#include <cwctype>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "include/cef_parser.h"
#include "include/cef_request.h"
#include "include/cef_values.h"
#include "platform/win/logger.h"

namespace velox::cef {

namespace {

constexpr std::array<std::wstring_view, 10> kAdHosts = {
    L"doubleclick.net",      L"googlesyndication.com", L"googleadservices.com", L"adnxs.com",
    L"adsrvr.org",           L"amazon-adsystem.com",   L"criteo.com",           L"criteo.net",
    L"pubmatic.com",         L"rubiconproject.com",
};

constexpr std::array<std::wstring_view, 14> kTrackerHosts = {
    L"google-analytics.com", L"googletagmanager.com", L"scorecardresearch.com", L"hotjar.com",
    L"segment.io",           L"segment.com",          L"mixpanel.com",          L"amplitude.com",
    L"newrelic.com",         L"nr-data.net",          L"fullstory.com",         L"appsflyer.com",
    L"connect.facebook.net", L"bat.bing.com",
};

constexpr std::array<std::wstring_view, 7> kTrackerQueryKeys = {
    L"fbclid",   L"gclid",     L"dclid",   L"msclkid",
    L"mc_cid",   L"mc_eid",    L"yclid",
};

constexpr std::array<std::wstring_view, 10> kUtmPrefixes = {
    L"utm_", L"vero_", L"ga_", L"ref_",
    L"mkt_", L"pk_",   L"spm", L"igshid",
    L"si",   L"rb_clickid",
};

constexpr std::array<std::wstring_view, 7> kTrackerPatterns = {
    L"/collect", L"analytics.js", L"/gtm.js", L"/pixel", L"/track", L"/telemetry", L"pagead",
};

std::wstring ToLower(std::wstring value) {
  for (wchar_t& ch : value) {
    ch = static_cast<wchar_t>(std::towlower(ch));
  }
  return value;
}

std::wstring ToLower(std::wstring_view value) {
  return ToLower(std::wstring(value));
}

std::wstring GetStringPart(const cef_string_t& value) {
  if (value.str == nullptr || value.length == 0) {
    return {};
  }
  return CefString(value.str, value.length).ToWString();
}

std::wstring ExtractHost(const std::wstring& url) {
  CefURLParts parts;
  if (!CefParseURL(url, parts)) {
    return {};
  }
  return ToLower(GetStringPart(parts.host));
}

std::wstring ExtractScheme(const std::wstring& url) {
  CefURLParts parts;
  if (!CefParseURL(url, parts)) {
    return {};
  }
  return ToLower(GetStringPart(parts.scheme));
}

bool IsLikelyIpAddress(std::wstring_view host) {
  if (host.empty()) {
    return false;
  }
  for (const wchar_t ch : host) {
    if ((ch >= L'0' && ch <= L'9') || ch == L'.' || ch == L':' || ch == L'[' || ch == L']') {
      continue;
    }
    return false;
  }
  return true;
}

std::wstring SiteKey(std::wstring host) {
  host = ToLower(std::move(host));
  while (!host.empty() && host.back() == L'.') {
    host.pop_back();
  }

  if (host.empty() || host == L"localhost" || IsLikelyIpAddress(host)) {
    return host;
  }

  const size_t last_dot = host.rfind(L'.');
  if (last_dot == std::wstring::npos) {
    return host;
  }

  const size_t second_last_dot = host.rfind(L'.', last_dot - 1);
  if (second_last_dot == std::wstring::npos) {
    return host;
  }

  const size_t third_last_dot = second_last_dot > 0 ? host.rfind(L'.', second_last_dot - 1) : std::wstring::npos;
  const size_t last_label_length = host.size() - last_dot - 1;
  const size_t second_label_length = last_dot - second_last_dot - 1;

  // This is a cheap eTLD+1 approximation so privacy policy decisions do not
  // need a heavyweight public suffix dependency in the hot request path.
  if (last_label_length == 2 && second_label_length <= 3 && third_last_dot != std::wstring::npos) {
    return host.substr(third_last_dot + 1);
  }
  return host.substr(second_last_dot + 1);
}

bool MatchesHostSuffix(std::wstring_view host, std::wstring_view suffix) {
  if (host == suffix) {
    return true;
  }
  if (host.size() <= suffix.size()) {
    return false;
  }
  const size_t offset = host.size() - suffix.size();
  return host[offset - 1] == L'.' && host.substr(offset) == suffix;
}

template <size_t N>
bool MatchesAnyHost(std::wstring_view host, const std::array<std::wstring_view, N>& suffixes) {
  for (const auto suffix : suffixes) {
    if (MatchesHostSuffix(host, suffix)) {
      return true;
    }
  }
  return false;
}

template <size_t N>
bool ContainsAnyPattern(std::wstring_view haystack, const std::array<std::wstring_view, N>& patterns) {
  for (const auto pattern : patterns) {
    if (haystack.find(pattern) != std::wstring_view::npos) {
      return true;
    }
  }
  return false;
}

bool StartsWith(std::wstring_view value, std::wstring_view prefix) {
  return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

bool IsTrackingParameter(std::wstring_view key) {
  for (const auto query_key : kTrackerQueryKeys) {
    if (key == query_key) {
      return true;
    }
  }
  for (const auto prefix : kUtmPrefixes) {
    if (StartsWith(key, prefix)) {
      return true;
    }
  }
  return false;
}

std::optional<std::wstring> StripTrackingParameters(std::wstring url) {
  const size_t query_pos = url.find(L'?');
  if (query_pos == std::wstring::npos) {
    return std::nullopt;
  }

  const size_t fragment_pos = url.find(L'#', query_pos + 1);
  const std::wstring base = url.substr(0, query_pos);
  const std::wstring fragment = fragment_pos == std::wstring::npos ? std::wstring{} : url.substr(fragment_pos);
  const std::wstring query =
      fragment_pos == std::wstring::npos ? url.substr(query_pos + 1) : url.substr(query_pos + 1, fragment_pos - query_pos - 1);

  std::vector<std::wstring> kept_tokens;
  bool changed = false;
  size_t start = 0;
  while (start <= query.size()) {
    const size_t end = query.find(L'&', start);
    const std::wstring token = query.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
    if (!token.empty()) {
      const size_t equals = token.find(L'=');
      const std::wstring key = ToLower(token.substr(0, equals));
      if (IsTrackingParameter(key)) {
        changed = true;
      } else {
        kept_tokens.push_back(token);
      }
    }

    if (end == std::wstring::npos) {
      break;
    }
    start = end + 1;
  }

  if (!changed) {
    return std::nullopt;
  }

  std::wstring rebuilt = base;
  if (!kept_tokens.empty()) {
    rebuilt += L"?";
    for (size_t index = 0; index < kept_tokens.size(); ++index) {
      if (index > 0) {
        rebuilt += L"&";
      }
      rebuilt += kept_tokens[index];
    }
  }
  rebuilt += fragment;
  return rebuilt;
}

bool IsHttpFamilyUrl(std::wstring_view url) {
  const std::wstring scheme = ExtractScheme(std::wstring(url));
  return scheme == L"http" || scheme == L"https";
}

void ApplyBooleanPreference(CefRefPtr<CefRequestContext> request_context, const char* name, bool value) {
  if (request_context == nullptr || !request_context->CanSetPreference(name)) {
    return;
  }

  CefRefPtr<CefValue> preference_value = CefValue::Create();
  preference_value->SetBool(value);
  CefString error;
  if (!request_context->SetPreference(name, preference_value, error)) {
    platform::LogWarning("Failed to set preference " + std::string(name) + ": " + error.ToString());
  }
}

void ApplyStringPreference(CefRefPtr<CefRequestContext> request_context, const char* name, std::string_view value) {
  if (request_context == nullptr || !request_context->CanSetPreference(name)) {
    return;
  }

  CefRefPtr<CefValue> preference_value = CefValue::Create();
  preference_value->SetString(std::string(value));
  CefString error;
  if (!request_context->SetPreference(name, preference_value, error)) {
    platform::LogWarning("Failed to set preference " + std::string(name) + ": " + error.ToString());
  }
}

std::wstring ExtractOrigin(const std::wstring& url) {
  CefURLParts parts;
  if (!CefParseURL(url, parts)) {
    return {};
  }

  const std::wstring scheme = GetStringPart(parts.scheme);
  const std::wstring host = GetStringPart(parts.host);
  const std::wstring port = GetStringPart(parts.port);
  if (scheme.empty() || host.empty()) {
    return {};
  }

  std::wstring origin = scheme + L"://" + host;
  if (!port.empty()) {
    origin += L":" + port;
  }
  return origin;
}

}  // namespace

BrowserPolicy::BrowserPolicy(settings::AppSettings settings, profiling::MetricsRecorder* metrics)
    : settings_(std::move(settings)), metrics_(metrics) {}

void BrowserPolicy::OnRequestContextInitialized(CefRefPtr<CefRequestContext> request_context) {
  ApplyContextPreferences(request_context);
}

CefRefPtr<CefResourceRequestHandler> BrowserPolicy::GetResourceRequestHandler(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request,
    bool is_navigation,
    bool is_download,
    const CefString& request_initiator,
    bool& disable_default_handling) {
  (void)browser;
  (void)frame;
  (void)request;
  (void)is_navigation;
  (void)is_download;
  (void)request_initiator;
  disable_default_handling = false;
  return this;
}

CefRefPtr<CefCookieAccessFilter> BrowserPolicy::GetCookieAccessFilter(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request) {
  (void)browser;
  (void)frame;
  (void)request;
  return settings_.privacy.block_third_party_cookies ? this : nullptr;
}

CefResourceRequestHandler::ReturnValue BrowserPolicy::OnBeforeResourceLoad(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request,
    CefRefPtr<CefCallback> callback) {
  (void)browser;
  (void)frame;
  (void)callback;

  if (request == nullptr) {
    return RV_CONTINUE;
  }

  const auto resource_type = request->GetResourceType();
  if (settings_.privacy.strip_tracking_query_parameters &&
      (resource_type == RT_MAIN_FRAME || resource_type == RT_SUB_FRAME)) {
    // Only rewrite top-level navigations so subresource URLs stay stable for
    // sites that key caches or signatures off exact request strings.
    if (const auto stripped = StripTrackingParameters(request->GetURL().ToWString()); stripped.has_value()) {
      request->SetURL(*stripped);
      platform::LogTrace("Stripped tracking parameters from navigation request.");
    }
  }

  if (ShouldBlockRequest(request)) {
    platform::LogTrace("Blocked ad or tracker request: " + request->GetURL().ToString());
    return RV_CANCEL;
  }

  ApplyReferrerPolicy(request);
  ApplyPrivacyHeaders(request);
  return RV_CONTINUE;
}

void BrowserPolicy::OnProtocolExecution(CefRefPtr<CefBrowser> browser,
                                        CefRefPtr<CefFrame> frame,
                                        CefRefPtr<CefRequest> request,
                                        bool& allow_os_execution) {
  (void)browser;
  (void)frame;
  if (!settings_.privacy.block_external_protocols) {
    return;
  }

  allow_os_execution = false;
  if (request != nullptr) {
    platform::LogTrace("Blocked external protocol execution for " + request->GetURL().ToString());
  }
}

bool BrowserPolicy::CanSendCookie(CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<CefFrame> frame,
                                  CefRefPtr<CefRequest> request,
                                  const CefCookie& cookie) {
  (void)browser;
  (void)frame;
  (void)cookie;
  return !settings_.privacy.block_third_party_cookies || !IsThirdPartyRequest(request);
}

bool BrowserPolicy::CanSaveCookie(CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<CefFrame> frame,
                                  CefRefPtr<CefRequest> request,
                                  CefRefPtr<CefResponse> response,
                                  const CefCookie& cookie) {
  (void)browser;
  (void)frame;
  (void)response;
  (void)cookie;
  return !settings_.privacy.block_third_party_cookies || !IsThirdPartyRequest(request);
}

bool BrowserPolicy::ShouldBlockRequest(CefRefPtr<CefRequest> request) const {
  if (request == nullptr || !settings_.blocking.enabled) {
    return false;
  }

  if (!IsHttpFamilyUrl(request->GetURL().ToWString())) {
    return false;
  }

  const auto resource_type = request->GetResourceType();
  if (resource_type == RT_MAIN_FRAME) {
    return false;
  }

  if (resource_type == RT_PING && settings_.blocking.block_trackers) {
    return true;
  }

  if (!IsThirdPartyRequest(request)) {
    return false;
  }

  const std::wstring url = ToLower(request->GetURL().ToWString());
  const std::wstring host = ExtractHost(url);
  if (host.empty()) {
    return false;
  }

  if (settings_.blocking.block_ads && MatchesAnyHost(host, kAdHosts)) {
    return true;
  }

  if (settings_.blocking.block_trackers &&
      (MatchesAnyHost(host, kTrackerHosts) || ContainsAnyPattern(url, kTrackerPatterns))) {
    return true;
  }

  return false;
}

void BrowserPolicy::ApplyPrivacyHeaders(CefRefPtr<CefRequest> request) const {
  if (request == nullptr || !IsHttpFamilyUrl(request->GetURL().ToWString())) {
    return;
  }

  if (settings_.privacy.do_not_track) {
    request->SetHeaderByName("DNT", "1", true);
  }
  if (settings_.privacy.global_privacy_control) {
    request->SetHeaderByName("Sec-GPC", "1", true);
  }
}

void BrowserPolicy::ApplyReferrerPolicy(CefRefPtr<CefRequest> request) const {
  if (request == nullptr || !settings_.privacy.strip_cross_site_referrers) {
    return;
  }

  const std::wstring referrer_url = request->GetReferrerURL().ToWString();
  if (!IsHttpFamilyUrl(request->GetURL().ToWString()) || !IsHttpFamilyUrl(referrer_url)) {
    return;
  }

  const std::wstring request_site = SiteKey(ExtractHost(request->GetURL().ToWString()));
  const std::wstring referrer_site = SiteKey(ExtractHost(referrer_url));
  if (request_site.empty() || referrer_site.empty() || request_site == referrer_site) {
    return;
  }

  const auto resource_type = request->GetResourceType();
  if (resource_type == RT_MAIN_FRAME || resource_type == RT_SUB_FRAME) {
    // Keep enough origin context for normal navigation flows while dropping the
    // path/query details that leak the most browsing intent cross-site.
    const std::wstring origin = ExtractOrigin(referrer_url);
    request->SetReferrer(origin, REFERRER_POLICY_ORIGIN_ONLY_ON_TRANSITION_CROSS_ORIGIN);
    return;
  }

  // For third-party subresources there is rarely any upside to exposing a
  // referrer at all, so we take the stricter option here.
  request->SetReferrer(std::wstring{}, REFERRER_POLICY_NO_REFERRER);
}

void BrowserPolicy::ApplyContextPreferences(CefRefPtr<CefRequestContext> request_context) const {
  ApplyBooleanPreference(request_context, "enable_do_not_track", settings_.privacy.do_not_track);
  ApplyBooleanPreference(request_context, "enable_hyperlink_auditing", false);

  if (settings_.privacy.block_third_party_cookies) {
    ApplyBooleanPreference(request_context, "profile.block_third_party_cookies", true);
  }

  if (settings_.privacy.block_webrtc_non_proxied_udp) {
    // This keeps WebRTC from bypassing the browser's normal network posture and
    // exposing local interfaces through direct UDP candidates.
    ApplyStringPreference(request_context, "webrtc.ip_handling_policy", "disable_non_proxied_udp");
    ApplyBooleanPreference(request_context, "webrtc.multiple_routes_enabled", false);
    ApplyBooleanPreference(request_context, "webrtc.nonproxied_udp_enabled", false);
  }

  if (settings_.privacy.disable_password_manager) {
    ApplyBooleanPreference(request_context, "credentials_enable_service", false);
    ApplyBooleanPreference(request_context, "profile.password_manager_enabled", false);
    ApplyBooleanPreference(request_context, "autofill.profile_enabled", false);
    ApplyBooleanPreference(request_context, "autofill.credit_card_enabled", false);
  }
}

bool BrowserPolicy::IsThirdPartyRequest(CefRefPtr<CefRequest> request) const {
  if (request == nullptr) {
    return false;
  }

  const std::wstring request_site = SiteKey(ExtractHost(request->GetURL().ToWString()));
  const std::wstring first_party_site = SiteKey(ExtractHost(request->GetFirstPartyForCookies().ToWString()));
  if (request_site.empty() || first_party_site.empty()) {
    return false;
  }
  return request_site != first_party_site;
}

}  // namespace velox::cef
