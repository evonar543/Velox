#pragma once

#include <string>

#include "include/cef_client.h"
#include "include/cef_download_handler.h"
#include "include/cef_keyboard_handler.h"
#include "include/cef_permission_handler.h"
#include "profiling/metrics_recorder.h"

namespace velox::cef {

enum class BrowserCommand {
  kFocusAddressBar,
  kNewTab,
  kNextTab,
  kPreviousTab,
  kGoBack,
  kGoForward,
  kReload,
  kStop,
  kCloseTabOrWindow,
  kToggleHistory,
  kToggleDownloads,
  kToggleSettings,
  kToggleBookmarks,
  kBookmarkCurrentPage,
};

class BrowserEventDelegate {
 public:
  virtual void OnBrowserCreated(int tab_id, CefRefPtr<CefBrowser> browser) = 0;
  virtual void OnBrowserClosed(int tab_id) = 0;
  virtual void OnAddressChanged(int tab_id, const std::wstring& url) = 0;
  virtual void OnTitleChanged(int tab_id, const std::wstring& title) = 0;
  virtual void OnLoadingStateChange(int tab_id, bool is_loading, bool can_go_back, bool can_go_forward) = 0;
  virtual void OnLoadError(int tab_id, const std::wstring& failed_url, const std::wstring& error_text) = 0;
  virtual void OnStatusMessage(int tab_id, const std::wstring& status) = 0;
  virtual void OnLoadProgress(int tab_id, double progress) = 0;
  virtual void OnRendererMetric(int tab_id, const std::string& name, double value) = 0;
  virtual void OnOpenUrlInNewTab(const std::wstring& url, bool activate) = 0;
  virtual std::wstring GetDownloadTargetPath(int tab_id, const std::wstring& suggested_name) = 0;
  virtual void OnDownloadCreated(int tab_id, int download_id, const std::wstring& file_name, const std::wstring& full_path) = 0;
  virtual void OnDownloadUpdated(int tab_id,
                                 int download_id,
                                 int percent_complete,
                                 bool is_complete,
                                 bool is_canceled,
                                 const std::wstring& status_text) = 0;
  virtual void OnBrowserCommand(BrowserCommand command) = 0;

 protected:
  ~BrowserEventDelegate() = default;
};

class VeloxClient : public CefClient,
                    public CefDisplayHandler,
                    public CefKeyboardHandler,
                    public CefLifeSpanHandler,
                    public CefLoadHandler,
                    public CefDownloadHandler,
                    public CefPermissionHandler,
                    public CefRequestHandler {
 public:
  VeloxClient(int tab_id,
              BrowserEventDelegate* delegate,
              profiling::MetricsRecorder* metrics,
              bool intercept_shell_shortcuts);

  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override;
  CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override;
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override;
  CefRefPtr<CefLoadHandler> GetLoadHandler() override;
  CefRefPtr<CefDownloadHandler> GetDownloadHandler() override;
  CefRefPtr<CefPermissionHandler> GetPermissionHandler() override;
  CefRefPtr<CefRequestHandler> GetRequestHandler() override;

  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
  bool OnBeforePopup(CefRefPtr<CefBrowser> browser,
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
                     bool* no_javascript_access) override;
  bool DoClose(CefRefPtr<CefBrowser> browser) override;
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;
  void OnAddressChange(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const CefString& url) override;
  void OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) override;
  void OnStatusMessage(CefRefPtr<CefBrowser> browser, const CefString& value) override;
  void OnLoadingProgressChange(CefRefPtr<CefBrowser> browser, double progress) override;
  bool OnPreKeyEvent(CefRefPtr<CefBrowser> browser,
                     const CefKeyEvent& event,
                     CefEventHandle os_event,
                     bool* is_keyboard_shortcut) override;
  void OnLoadingStateChange(CefRefPtr<CefBrowser> browser, bool is_loading, bool can_go_back, bool can_go_forward) override;
  void OnLoadStart(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, TransitionType transition_type) override;
  void OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int http_status_code) override;
  void OnLoadError(CefRefPtr<CefBrowser> browser,
                   CefRefPtr<CefFrame> frame,
                   ErrorCode error_code,
                   const CefString& error_text,
                   const CefString& failed_url) override;
  bool OnBeforeDownload(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefDownloadItem> download_item,
                        const CefString& suggested_name,
                        CefRefPtr<CefBeforeDownloadCallback> callback) override;
  void OnDownloadUpdated(CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefDownloadItem> download_item,
                         CefRefPtr<CefDownloadItemCallback> callback) override;
  bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                      CefRefPtr<CefFrame> frame,
                      CefRefPtr<CefRequest> request,
                      bool user_gesture,
                      bool is_redirect) override;
  bool OnOpenURLFromTab(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        const CefString& target_url,
                        CefRequestHandler::WindowOpenDisposition target_disposition,
                        bool user_gesture) override;
  bool OnRequestMediaAccessPermission(CefRefPtr<CefBrowser> browser,
                                      CefRefPtr<CefFrame> frame,
                                      const CefString& requesting_origin,
                                      uint32_t requested_permissions,
                                      CefRefPtr<CefMediaAccessCallback> callback) override;
  bool OnShowPermissionPrompt(CefRefPtr<CefBrowser> browser,
                              uint64_t prompt_id,
                              const CefString& requesting_origin,
                              uint32_t requested_permissions,
                              CefRefPtr<CefPermissionPromptCallback> callback) override;
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefProcessId source_process,
                                CefRefPtr<CefProcessMessage> message) override;

 private:
  bool TryHandleShortcut(const CefKeyEvent& event) const;
  void RecordPermissionDecision(std::string_view event_name,
                                std::string_view permission_name,
                                const CefString& requesting_origin) const;

  int tab_id_ = 0;
  BrowserEventDelegate* delegate_ = nullptr;
  profiling::MetricsRecorder* metrics_ = nullptr;
  bool intercept_shell_shortcuts_ = true;
  bool first_main_frame_load_seen_ = false;

  IMPLEMENT_REFCOUNTING(VeloxClient);
};

}  // namespace velox::cef
