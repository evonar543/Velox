#pragma once

#include <Windows.h>

#include <mutex>
#include <optional>
#include <string>

#include "app/command_line.h"
#include "browser/browser_controller.h"
#include "cef/velox_client.h"
#include "include/cef_browser.h"
#include "include/cef_request_context.h"
#include "profiling/metrics_recorder.h"
#include "settings/app_settings.h"

namespace velox::browser {

class BrowserWindow : public cef::BrowserEventDelegate {
 public:
  BrowserWindow(HINSTANCE instance,
                settings::AppSettings settings,
                app::CommandLineOptions command_line,
                profiling::MetricsRecorder* metrics);

  bool Create();
  void Show(int command_show);
  bool CreateBrowserShell();
  HWND hwnd() const;

  void OnBrowserCreated(CefRefPtr<CefBrowser> browser) override;
  void OnBrowserClosed() override;
  void OnAddressChanged(const std::wstring& url) override;
  void OnTitleChanged(const std::wstring& title) override;
  void OnLoadingStateChange(bool is_loading, bool can_go_back, bool can_go_forward) override;
  void OnLoadError(const std::wstring& failed_url, const std::wstring& error_text) override;
  void OnRendererMetric(const std::string& name, double value) override;

 private:
  struct PendingLoadState {
    bool is_loading = false;
    bool can_go_back = false;
    bool can_go_forward = false;
  };

  static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
  LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);

  void CreateControls();
  void LayoutChildren();
  void ResizeBrowserHost();
  void NavigateFromAddressBar();
  void UpdateNavigationButtons();
  void SetAddressBarText(const std::wstring& text);
  void PostWindowMessage(UINT message);

  void HandleBrowserCreatedMessage();
  void HandleBrowserClosedMessage();
  void HandleAddressChangedMessage();
  void HandleTitleChangedMessage();
  void HandleLoadingStateMessage();
  void HandleLoadErrorMessage();

  HINSTANCE instance_ = nullptr;
  settings::AppSettings settings_;
  app::CommandLineOptions command_line_;
  profiling::MetricsRecorder* metrics_ = nullptr;

  HWND hwnd_ = nullptr;
  HWND back_button_ = nullptr;
  HWND forward_button_ = nullptr;
  HWND reload_button_ = nullptr;
  HWND stop_button_ = nullptr;
  HWND address_bar_ = nullptr;

  browser::BrowserController controller_;
  CefRefPtr<cef::VeloxClient> client_;
  CefRefPtr<CefRequestContext> request_context_;

  mutable std::mutex pending_mutex_;
  CefRefPtr<CefBrowser> pending_browser_;
  std::wstring pending_address_;
  std::wstring pending_title_;
  PendingLoadState pending_load_state_;
  std::wstring pending_error_text_;
  std::wstring pending_failed_url_;

  bool close_requested_ = false;
  bool quit_after_load_posted_ = false;
  bool first_navigation_requested_ = false;
  bool saw_loading_activity_ = false;
};

}  // namespace velox::browser
