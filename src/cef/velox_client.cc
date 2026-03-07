#include "cef/velox_client.h"

#include <array>
#include <sstream>

#include "cef/render_metrics_bridge.h"
#include "platform/win/logger.h"

namespace velox::cef {

namespace {

bool HasModifier(int modifiers, cef_event_flags_t flag) {
  return (modifiers & flag) != 0;
}

std::string DescribeMediaPermissions(uint32_t requested_permissions) {
  if ((requested_permissions & CEF_MEDIA_PERMISSION_DEVICE_AUDIO_CAPTURE) != 0 &&
      (requested_permissions & CEF_MEDIA_PERMISSION_DEVICE_VIDEO_CAPTURE) != 0) {
    return "camera+microphone";
  }
  if ((requested_permissions & CEF_MEDIA_PERMISSION_DEVICE_VIDEO_CAPTURE) != 0) {
    return "camera";
  }
  if ((requested_permissions & CEF_MEDIA_PERMISSION_DEVICE_AUDIO_CAPTURE) != 0) {
    return "microphone";
  }
  return "media";
}

std::string DescribePermissionTypes(uint32_t requested_permissions) {
  struct PermissionLabel {
    uint32_t mask;
    const char* label;
  };

  constexpr std::array<PermissionLabel, 6> kKnownPermissions = {{
      {CEF_PERMISSION_TYPE_NOTIFICATIONS, "notifications"},
      {CEF_PERMISSION_TYPE_GEOLOCATION, "geolocation"},
      {CEF_PERMISSION_TYPE_CAMERA_STREAM, "camera"},
      {CEF_PERMISSION_TYPE_MIC_STREAM, "microphone"},
      {CEF_PERMISSION_TYPE_CLIPBOARD, "clipboard"},
      {CEF_PERMISSION_TYPE_MULTIPLE_DOWNLOADS, "multiple-downloads"},
  }};

  std::string summary;
  for (const auto& permission : kKnownPermissions) {
    if ((requested_permissions & permission.mask) == 0) {
      continue;
    }
    if (!summary.empty()) {
      summary += "+";
    }
    summary += permission.label;
  }

  return summary.empty() ? "permission" : summary;
}

}  // namespace

VeloxClient::VeloxClient(int tab_id, BrowserEventDelegate* delegate, profiling::MetricsRecorder* metrics)
    : tab_id_(tab_id), delegate_(delegate), metrics_(metrics) {}

CefRefPtr<CefDisplayHandler> VeloxClient::GetDisplayHandler() {
  return this;
}

CefRefPtr<CefKeyboardHandler> VeloxClient::GetKeyboardHandler() {
  return this;
}

CefRefPtr<CefLifeSpanHandler> VeloxClient::GetLifeSpanHandler() {
  return this;
}

CefRefPtr<CefLoadHandler> VeloxClient::GetLoadHandler() {
  return this;
}

CefRefPtr<CefDownloadHandler> VeloxClient::GetDownloadHandler() {
  return this;
}

CefRefPtr<CefPermissionHandler> VeloxClient::GetPermissionHandler() {
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
    delegate_->OnBrowserCreated(tab_id_, browser);
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

  if (delegate_ != nullptr && !target_url.empty()) {
    delegate_->OnOpenUrlInNewTab(target_url.ToWString(), true);
    if (metrics_ != nullptr) {
      metrics_->Mark("popup.opened_in_new_tab");
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
    delegate_->OnBrowserClosed(tab_id_);
  }
}

void VeloxClient::OnAddressChange(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const CefString& url) {
  (void)browser;
  if (delegate_ != nullptr && frame != nullptr && frame->IsMain()) {
    delegate_->OnAddressChanged(tab_id_, url.ToWString());
  }
}

void VeloxClient::OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) {
  (void)browser;
  if (delegate_ != nullptr) {
    delegate_->OnTitleChanged(tab_id_, title.ToWString());
  }
}

void VeloxClient::OnStatusMessage(CefRefPtr<CefBrowser> browser, const CefString& value) {
  (void)browser;
  if (delegate_ != nullptr) {
    delegate_->OnStatusMessage(tab_id_, value.ToWString());
  }
}

void VeloxClient::OnLoadingProgressChange(CefRefPtr<CefBrowser> browser, double progress) {
  (void)browser;
  if (delegate_ != nullptr) {
    delegate_->OnLoadProgress(tab_id_, progress);
  }
}

bool VeloxClient::OnPreKeyEvent(CefRefPtr<CefBrowser> browser,
                                const CefKeyEvent& event,
                                CefEventHandle os_event,
                                bool* is_keyboard_shortcut) {
  (void)browser;
  (void)os_event;

  if (is_keyboard_shortcut != nullptr) {
    *is_keyboard_shortcut = false;
  }

  if (event.type != KEYEVENT_RAWKEYDOWN && event.type != KEYEVENT_KEYDOWN) {
    return false;
  }

  const bool handled = TryHandleShortcut(event);
  if (handled && is_keyboard_shortcut != nullptr) {
    *is_keyboard_shortcut = true;
  }
  return handled;
}

void VeloxClient::OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                                       bool is_loading,
                                       bool can_go_back,
                                       bool can_go_forward) {
  (void)browser;
  if (delegate_ != nullptr) {
    delegate_->OnLoadingStateChange(tab_id_, is_loading, can_go_back, can_go_forward);
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
    delegate_->OnLoadError(tab_id_, failed_url.ToWString(), error_text.ToWString());
  }
}

bool VeloxClient::OnBeforeDownload(CefRefPtr<CefBrowser> browser,
                                   CefRefPtr<CefDownloadItem> download_item,
                                   const CefString& suggested_name,
                                   CefRefPtr<CefBeforeDownloadCallback> callback) {
  (void)browser;

  if (delegate_ == nullptr || download_item == nullptr || callback == nullptr) {
    return false;
  }

  std::wstring file_name = suggested_name.empty() ? L"download.bin" : suggested_name.ToWString();
  const std::wstring full_path = delegate_->GetDownloadTargetPath(tab_id_, file_name);
  if (full_path.empty()) {
    callback->Continue(file_name, false);
    return true;
  }

  delegate_->OnDownloadCreated(tab_id_, static_cast<int>(download_item->GetId()), file_name, full_path);
  if (metrics_ != nullptr) {
    metrics_->RecordText("download.started", std::to_string(download_item->GetId()) + ":" + CefString(full_path).ToString());
  }
  callback->Continue(full_path, false);
  return true;
}

void VeloxClient::OnDownloadUpdated(CefRefPtr<CefBrowser> browser,
                                    CefRefPtr<CefDownloadItem> download_item,
                                    CefRefPtr<CefDownloadItemCallback> callback) {
  (void)browser;
  (void)callback;

  if (delegate_ == nullptr || download_item == nullptr) {
    return;
  }

  const int percent_complete = download_item->GetPercentComplete();
  std::wstring status_text = L"Preparing download";
  if (download_item->IsComplete()) {
    status_text = L"Finished";
  } else if (download_item->IsCanceled()) {
    status_text = L"Canceled";
  } else if (percent_complete >= 0) {
    status_text = L"Downloading " + std::to_wstring(percent_complete) + L"%";
  }

  delegate_->OnDownloadUpdated(tab_id_,
                               static_cast<int>(download_item->GetId()),
                               percent_complete,
                               download_item->IsComplete(),
                               download_item->IsCanceled(),
                               status_text);

  if (metrics_ != nullptr) {
    if (download_item->IsComplete()) {
      metrics_->Mark("download.complete");
    }
    metrics_->RecordNumeric("download.percent", static_cast<double>(std::max(percent_complete, 0)));
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

  if (delegate_ != nullptr) {
    delegate_->OnOpenUrlInNewTab(target_url.ToWString(), true);
  }
  if (metrics_ != nullptr) {
    metrics_->Mark("navigation.opened_in_new_tab");
  }
  return true;
}

bool VeloxClient::OnRequestMediaAccessPermission(CefRefPtr<CefBrowser> browser,
                                                 CefRefPtr<CefFrame> frame,
                                                 const CefString& requesting_origin,
                                                 uint32_t requested_permissions,
                                                 CefRefPtr<CefMediaAccessCallback> callback) {
  (void)browser;
  (void)frame;

  if (callback != nullptr) {
    callback->Cancel();
  }
  RecordPermissionDecision("permission.media.denied", DescribeMediaPermissions(requested_permissions), requesting_origin);
  if (delegate_ != nullptr) {
    delegate_->OnStatusMessage(tab_id_, L"Blocked camera or microphone access");
  }
  return true;
}

bool VeloxClient::OnShowPermissionPrompt(CefRefPtr<CefBrowser> browser,
                                         uint64_t prompt_id,
                                         const CefString& requesting_origin,
                                         uint32_t requested_permissions,
                                         CefRefPtr<CefPermissionPromptCallback> callback) {
  (void)browser;
  (void)prompt_id;

  if (callback != nullptr) {
    callback->Continue(CEF_PERMISSION_RESULT_DENY);
  }
  RecordPermissionDecision("permission.prompt.denied", DescribePermissionTypes(requested_permissions), requesting_origin);
  if (delegate_ != nullptr) {
    delegate_->OnStatusMessage(tab_id_, L"Blocked site permission request");
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
    delegate_->OnRendererMetric(tab_id_, name, value);
  }
  return true;
}

bool VeloxClient::TryHandleShortcut(const CefKeyEvent& event) const {
  if (delegate_ == nullptr) {
    return false;
  }

  const bool control_down = HasModifier(event.modifiers, EVENTFLAG_CONTROL_DOWN);
  const bool alt_down = HasModifier(event.modifiers, EVENTFLAG_ALT_DOWN);
  switch (event.windows_key_code) {
    case 'L':
      if (control_down) {
        delegate_->OnBrowserCommand(BrowserCommand::kFocusAddressBar);
        return true;
      }
      break;
    case 'T':
      if (control_down) {
        delegate_->OnBrowserCommand(BrowserCommand::kNewTab);
        return true;
      }
      break;
    case 'R':
      if (control_down) {
        delegate_->OnBrowserCommand(BrowserCommand::kReload);
        return true;
      }
      break;
    case 'H':
      if (control_down) {
        delegate_->OnBrowserCommand(BrowserCommand::kToggleHistory);
        return true;
      }
      break;
    case 'B':
      if (control_down) {
        delegate_->OnBrowserCommand(BrowserCommand::kToggleBookmarks);
        return true;
      }
      break;
    case 'D':
      if (control_down) {
        delegate_->OnBrowserCommand(BrowserCommand::kBookmarkCurrentPage);
        return true;
      }
      break;
    case 'J':
      if (control_down) {
        delegate_->OnBrowserCommand(BrowserCommand::kToggleDownloads);
        return true;
      }
      break;
    case 'W':
      if (control_down) {
        delegate_->OnBrowserCommand(BrowserCommand::kCloseTabOrWindow);
        return true;
      }
      break;
    case VK_TAB:
      if (control_down && alt_down) {
        return false;
      }
      if (control_down) {
        delegate_->OnBrowserCommand(HasModifier(event.modifiers, EVENTFLAG_SHIFT_DOWN) ? BrowserCommand::kPreviousTab
                                                                                       : BrowserCommand::kNextTab);
        return true;
      }
      break;
    case VK_F5:
      delegate_->OnBrowserCommand(BrowserCommand::kReload);
      return true;
    case VK_ESCAPE:
      delegate_->OnBrowserCommand(BrowserCommand::kStop);
      return true;
    case VK_F6:
      delegate_->OnBrowserCommand(BrowserCommand::kFocusAddressBar);
      return true;
    case VK_OEM_COMMA:
      if (control_down) {
        delegate_->OnBrowserCommand(BrowserCommand::kToggleSettings);
        return true;
      }
      break;
    case VK_LEFT:
      if (alt_down) {
        delegate_->OnBrowserCommand(BrowserCommand::kGoBack);
        return true;
      }
      break;
    case VK_RIGHT:
      if (alt_down) {
        delegate_->OnBrowserCommand(BrowserCommand::kGoForward);
        return true;
      }
      break;
  }

  return false;
}

void VeloxClient::RecordPermissionDecision(std::string_view event_name,
                                           std::string_view permission_name,
                                           const CefString& requesting_origin) const {
  const std::string origin = requesting_origin.ToString();
  platform::LogInfo("Denied " + std::string(permission_name) + " permission for " + origin);
  if (metrics_ != nullptr) {
    metrics_->RecordText(event_name, std::string(permission_name) + "@" + origin);
  }
}

}  // namespace velox::cef
