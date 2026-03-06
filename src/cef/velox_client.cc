#include "cef/velox_client.h"

#include <sstream>

#include "cef/render_metrics_bridge.h"
#include "platform/win/logger.h"

namespace velox::cef {

VeloxClient::VeloxClient(BrowserEventDelegate* delegate, profiling::MetricsRecorder* metrics)
    : delegate_(delegate), metrics_(metrics) {}

CefRefPtr<CefDisplayHandler> VeloxClient::GetDisplayHandler() {
  return this;
}

CefRefPtr<CefLifeSpanHandler> VeloxClient::GetLifeSpanHandler() {
  return this;
}

CefRefPtr<CefLoadHandler> VeloxClient::GetLoadHandler() {
  return this;
}

CefRefPtr<CefRequestHandler> VeloxClient::GetRequestHandler() {
  return this;
}

void VeloxClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  if (metrics_ != nullptr) {
    metrics_->Mark("browser.created");
    metrics_->RecordMemory("browser.created");
  }
  if (delegate_ != nullptr) {
    delegate_->OnBrowserCreated(browser);
  }
}

bool VeloxClient::OnBeforePopup(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                int popup_id,
                                const CefString& target_url,
                                const CefString& target_frame_name,
                                CefLifeSpanHandler::WindowOpenDisposition target_disposition,
                                bool user_gesture,
                                const CefPopupFeatures& popupFeatures,
                                CefWindowInfo& windowInfo,
                                CefRefPtr<CefClient>& client,
                                CefBrowserSettings& settings,
                                CefRefPtr<CefDictionaryValue>& extra_info,
                                bool* no_javascript_access) {
  (void)frame;
  (void)popup_id;
  (void)target_frame_name;
  (void)target_disposition;
  (void)user_gesture;
  (void)popupFeatures;
  (void)windowInfo;
  (void)client;
  (void)settings;
  (void)extra_info;
  (void)no_javascript_access;

  if (browser != nullptr && !target_url.empty()) {
    browser->GetMainFrame()->LoadURL(target_url);
    if (metrics_ != nullptr) {
      metrics_->Mark("popup.collapsed_to_current_tab");
    }
  }
  return true;
}

bool VeloxClient::DoClose(CefRefPtr<CefBrowser> browser) {
  (void)browser;
  return false;
}

void VeloxClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  (void)browser;
  if (delegate_ != nullptr) {
    delegate_->OnBrowserClosed();
  }
}

void VeloxClient::OnAddressChange(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const CefString& url) {
  (void)browser;
  if (delegate_ != nullptr && frame != nullptr && frame->IsMain()) {
    delegate_->OnAddressChanged(url.ToWString());
  }
}

void VeloxClient::OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) {
  (void)browser;
  if (delegate_ != nullptr) {
    delegate_->OnTitleChanged(title.ToWString());
  }
}

void VeloxClient::OnStatusMessage(CefRefPtr<CefBrowser> browser, const CefString& value) {
  (void)browser;
  if (delegate_ != nullptr) {
    delegate_->OnStatusMessage(value.ToWString());
  }
}

void VeloxClient::OnLoadingProgressChange(CefRefPtr<CefBrowser> browser, double progress) {
  (void)browser;
  if (delegate_ != nullptr) {
    delegate_->OnLoadProgress(progress);
  }
}

void VeloxClient::OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                                       bool is_loading,
                                       bool can_go_back,
                                       bool can_go_forward) {
  (void)browser;
  if (delegate_ != nullptr) {
    delegate_->OnLoadingStateChange(is_loading, can_go_back, can_go_forward);
  }

  if (metrics_ != nullptr) {
    metrics_->RecordText("load.state", is_loading ? "loading" : "idle");
    if (!is_loading) {
      metrics_->RecordMemory("load.complete");
    }
  }
}

void VeloxClient::OnLoadStart(CefRefPtr<CefBrowser> browser,
                              CefRefPtr<CefFrame> frame,
                              TransitionType transition_type) {
  (void)browser;
  (void)transition_type;
  if (metrics_ != nullptr && frame != nullptr && frame->IsMain()) {
    metrics_->Mark(first_main_frame_load_seen_ ? "navigation.start" : "first_navigation.start");
    first_main_frame_load_seen_ = true;
  }
}

void VeloxClient::OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int http_status_code) {
  (void)browser;
  if (metrics_ != nullptr && frame != nullptr && frame->IsMain()) {
    metrics_->RecordNumeric("navigation.http_status", static_cast<double>(http_status_code));
    metrics_->Mark("navigation.end");
  }
}

void VeloxClient::OnLoadError(CefRefPtr<CefBrowser> browser,
                              CefRefPtr<CefFrame> frame,
                              ErrorCode error_code,
                              const CefString& error_text,
                              const CefString& failed_url) {
  (void)browser;
  if (frame == nullptr || !frame->IsMain()) {
    return;
  }

  std::ostringstream stream;
  stream << "Load error " << static_cast<int>(error_code) << " for " << failed_url.ToString();
  platform::LogWarning(stream.str());

  if (metrics_ != nullptr) {
    metrics_->RecordText("navigation.error", error_text.ToString());
  }
  if (delegate_ != nullptr) {
    delegate_->OnLoadError(failed_url.ToWString(), error_text.ToWString());
  }
}

bool VeloxClient::OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                                 CefRefPtr<CefFrame> frame,
                                 CefRefPtr<CefRequest> request,
                                 bool user_gesture,
                                 bool is_redirect) {
  (void)browser;
  (void)frame;
  (void)user_gesture;
  (void)is_redirect;

  if (metrics_ != nullptr && request != nullptr) {
    metrics_->RecordText("navigation.url", request->GetURL().ToString());
  }
  return false;
}

bool VeloxClient::OnOpenURLFromTab(CefRefPtr<CefBrowser> browser,
                                   CefRefPtr<CefFrame> frame,
                                   const CefString& target_url,
                                   CefRequestHandler::WindowOpenDisposition target_disposition,
                                   bool user_gesture) {
  (void)frame;
  (void)target_disposition;
  (void)user_gesture;

  if (browser == nullptr || target_url.empty()) {
    return false;
  }

  browser->GetMainFrame()->LoadURL(target_url);
  if (metrics_ != nullptr) {
    metrics_->Mark("navigation.collapsed_to_current_tab");
  }
  return true;
}

bool VeloxClient::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                           CefRefPtr<CefFrame> frame,
                                           CefProcessId source_process,
                                           CefRefPtr<CefProcessMessage> message) {
  (void)browser;
  (void)frame;
  (void)source_process;

  std::string name;
  double value = 0.0;
  if (!TryReadRendererMetric(message, &name, &value)) {
    return false;
  }

  if (metrics_ != nullptr) {
    metrics_->RecordNumeric(name, value);
  }
  if (delegate_ != nullptr) {
    delegate_->OnRendererMetric(name, value);
  }
  return true;
}

}  // namespace velox::cef
