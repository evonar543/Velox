#include "cef/request_context_factory.h"

#include "cef/browser_policy.h"

namespace velox::cef {

CefRefPtr<CefRequestContext> CreateRequestContext(const settings::AppSettings& settings,
                                                  bool incognito,
                                                  profiling::MetricsRecorder* metrics) {
  CefRequestContextSettings request_context_settings;
  request_context_settings.persist_session_cookies = false;
  if (!incognito) {
    // Keep normal browsing persistent, but let incognito stay memory-backed by
    // leaving the cache path empty.
    CefString(&request_context_settings.cache_path) = settings.paths.cache_dir.wstring();
  }
  return CefRequestContext::CreateContext(request_context_settings, new BrowserPolicy(settings, metrics));
}

}  // namespace velox::cef
