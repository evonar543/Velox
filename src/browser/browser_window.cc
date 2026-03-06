#include "browser/browser_window.h"

#include <commctrl.h>

#include <utility>

#include "browser/win32_controls.h"
#include "cef/request_context_factory.h"
#include "platform/win/file_utils.h"
#include "platform/win/logger.h"

namespace velox::browser {

namespace {

constexpr wchar_t kWindowClassName[] = L"VeloxBrowserWindow";
constexpr int kAppIconResourceId = 101;
constexpr UINT kMessageBrowserCreated = WM_APP + 1;
constexpr UINT kMessageBrowserClosed = WM_APP + 2;
constexpr UINT kMessageAddressChanged = WM_APP + 3;
constexpr UINT kMessageTitleChanged = WM_APP + 4;
constexpr UINT kMessageLoadingState = WM_APP + 5;
constexpr UINT kMessageLoadError = WM_APP + 6;

std::wstring LoadTextFromWindow(HWND window) {
  const int length = GetWindowTextLengthW(window);
  if (length <= 0) {
    return {};
  }

  std::wstring text(static_cast<size_t>(length) + 1, L'\0');
  const int copied = GetWindowTextW(window, text.data(), length + 1);
  text.resize(static_cast<size_t>(copied));
  return text;
}

}  // namespace

BrowserWindow::BrowserWindow(HINSTANCE instance,
                             settings::AppSettings settings,
                             app::CommandLineOptions command_line,
                             profiling::MetricsRecorder* metrics)
    : instance_(instance),
      settings_(std::move(settings)),
      command_line_(std::move(command_line)),
      metrics_(metrics) {}

bool BrowserWindow::Create() {
  INITCOMMONCONTROLSEX common_controls{};
  common_controls.dwSize = sizeof(common_controls);
  common_controls.dwICC = ICC_STANDARD_CLASSES;
  InitCommonControlsEx(&common_controls);

  WNDCLASSEXW window_class{};
  window_class.cbSize = sizeof(window_class);
  window_class.lpfnWndProc = &BrowserWindow::WindowProc;
  window_class.hInstance = instance_;
  window_class.lpszClassName = kWindowClassName;
  window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  window_class.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(kAppIconResourceId));
  if (window_class.hIcon == nullptr) {
    window_class.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  }
  window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  RegisterClassExW(&window_class);

  hwnd_ = CreateWindowExW(0,
                          kWindowClassName,
                          L"Velox",
                          WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                          CW_USEDEFAULT,
                          CW_USEDEFAULT,
                          settings_.window.width,
                          settings_.window.height,
                          nullptr,
                          nullptr,
                          instance_,
                          this);
  return hwnd_ != nullptr;
}

void BrowserWindow::Show(int command_show) {
  ShowWindow(hwnd_, command_show);
  UpdateWindow(hwnd_);
}

bool BrowserWindow::CreateBrowserShell() {
  if (hwnd_ == nullptr) {
    return false;
  }

  // Keep the shell native and thin so startup work stays focused on Chromium,
  // not on a second UI abstraction layer we do not need yet.
  client_ = new cef::VeloxClient(this, metrics_);
  request_context_ = cef::CreateRequestContext(settings_, settings_.incognito_default, metrics_);

  RECT client_rect{};
  GetClientRect(hwnd_, &client_rect);
  const auto layout = win32::ComputeLayout(client_rect);
  const CefRect browser_bounds(layout.browser.left,
                               layout.browser.top,
                               layout.browser.right - layout.browser.left,
                               layout.browser.bottom - layout.browser.top);

  CefWindowInfo window_info;
  window_info.SetAsChild(hwnd_, browser_bounds);

  CefBrowserSettings browser_settings;
  browser_settings.background_color = CefColorSetARGB(255, 255, 255, 255);

  if (!first_navigation_requested_ && metrics_ != nullptr) {
    metrics_->Mark("first_navigation.requested");
    first_navigation_requested_ = true;
  }

  SetAddressBarText(settings_.startup_url);
  return CefBrowserHost::CreateBrowser(window_info,
                                       client_.get(),
                                       settings_.startup_url,
                                       browser_settings,
                                       nullptr,
                                       request_context_);
}

HWND BrowserWindow::hwnd() const {
  return hwnd_;
}

void BrowserWindow::OnBrowserCreated(CefRefPtr<CefBrowser> browser) {
  {
    std::scoped_lock lock(pending_mutex_);
    pending_browser_ = browser;
  }
  PostWindowMessage(kMessageBrowserCreated);
}

void BrowserWindow::OnBrowserClosed() {
  PostWindowMessage(kMessageBrowserClosed);
}

void BrowserWindow::OnAddressChanged(const std::wstring& url) {
  {
    std::scoped_lock lock(pending_mutex_);
    pending_address_ = url;
  }
  PostWindowMessage(kMessageAddressChanged);
}

void BrowserWindow::OnTitleChanged(const std::wstring& title) {
  {
    std::scoped_lock lock(pending_mutex_);
    pending_title_ = title;
  }
  PostWindowMessage(kMessageTitleChanged);
}

void BrowserWindow::OnLoadingStateChange(bool is_loading, bool can_go_back, bool can_go_forward) {
  {
    std::scoped_lock lock(pending_mutex_);
    pending_load_state_.is_loading = is_loading;
    pending_load_state_.can_go_back = can_go_back;
    pending_load_state_.can_go_forward = can_go_forward;
  }
  PostWindowMessage(kMessageLoadingState);
}

void BrowserWindow::OnLoadError(const std::wstring& failed_url, const std::wstring& error_text) {
  {
    std::scoped_lock lock(pending_mutex_);
    pending_failed_url_ = failed_url;
    pending_error_text_ = error_text;
  }
  PostWindowMessage(kMessageLoadError);
}

void BrowserWindow::OnRendererMetric(const std::string& name, double value) {
  (void)name;
  (void)value;
}

LRESULT CALLBACK BrowserWindow::WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  BrowserWindow* self = nullptr;
  if (message == WM_NCCREATE) {
    auto* create_struct = reinterpret_cast<CREATESTRUCTW*>(lparam);
    self = reinterpret_cast<BrowserWindow*>(create_struct->lpCreateParams);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    self->hwnd_ = window;
  } else {
    self = reinterpret_cast<BrowserWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  }

  if (self == nullptr) {
    return DefWindowProcW(window, message, wparam, lparam);
  }
  return self->HandleMessage(message, wparam, lparam);
}

LRESULT BrowserWindow::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_CREATE:
      CreateControls();
      LayoutChildren();
      return 0;
    case WM_COMMAND: {
      const WORD control_id = LOWORD(wparam);
      const WORD notification_code = HIWORD(wparam);
      switch (control_id) {
        case win32::kBackButtonId:
          controller_.GoBack();
          return 0;
        case win32::kForwardButtonId:
          controller_.GoForward();
          return 0;
        case win32::kReloadButtonId:
          controller_.Reload();
          return 0;
        case win32::kStopButtonId:
          controller_.Stop();
          return 0;
        case win32::kAddressBarId:
          if (notification_code == win32::kAddressEnterNotification) {
            NavigateFromAddressBar();
            return 0;
          }
          break;
      }
      break;
    }
    case WM_SIZE:
      LayoutChildren();
      ResizeBrowserHost();
      return 0;
    case WM_CLOSE:
      if (controller_.has_browser()) {
        if (!close_requested_) {
          close_requested_ = true;
          controller_.CloseBrowser();
        }
        return 0;
      }
      DestroyWindow(hwnd_);
      return 0;
    case WM_DESTROY:
      hwnd_ = nullptr;
      PostQuitMessage(0);
      return 0;
    case kMessageBrowserCreated:
      HandleBrowserCreatedMessage();
      return 0;
    case kMessageBrowserClosed:
      HandleBrowserClosedMessage();
      return 0;
    case kMessageAddressChanged:
      HandleAddressChangedMessage();
      return 0;
    case kMessageTitleChanged:
      HandleTitleChangedMessage();
      return 0;
    case kMessageLoadingState:
      HandleLoadingStateMessage();
      return 0;
    case kMessageLoadError:
      HandleLoadErrorMessage();
      return 0;
  }

  return DefWindowProcW(hwnd_, message, wparam, lparam);
}

void BrowserWindow::CreateControls() {
  back_button_ = CreateWindowExW(0, L"BUTTON", L"Back", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd_,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(win32::kBackButtonId)), instance_, nullptr);
  forward_button_ = CreateWindowExW(0, L"BUTTON", L"Forward", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd_,
                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(win32::kForwardButtonId)), instance_, nullptr);
  reload_button_ = CreateWindowExW(0, L"BUTTON", L"Reload", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd_,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(win32::kReloadButtonId)), instance_, nullptr);
  stop_button_ = CreateWindowExW(0, L"BUTTON", L"Stop", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd_,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(win32::kStopButtonId)), instance_, nullptr);
  address_bar_ = CreateWindowExW(WS_EX_CLIENTEDGE,
                                 L"EDIT",
                                 settings_.startup_url.c_str(),
                                 WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                 0,
                                 0,
                                 0,
                                 0,
                                 hwnd_,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(win32::kAddressBarId)),
                                 instance_,
                                 nullptr);
  win32::InstallAddressBarSubclass(address_bar_, hwnd_);
  UpdateNavigationButtons();
}

void BrowserWindow::LayoutChildren() {
  if (hwnd_ == nullptr) {
    return;
  }

  RECT client_rect{};
  GetClientRect(hwnd_, &client_rect);
  const auto layout = win32::ComputeLayout(client_rect);

  MoveWindow(back_button_, layout.back.left, layout.back.top, layout.back.right - layout.back.left, layout.back.bottom - layout.back.top, TRUE);
  MoveWindow(forward_button_, layout.forward.left, layout.forward.top, layout.forward.right - layout.forward.left, layout.forward.bottom - layout.forward.top, TRUE);
  MoveWindow(reload_button_, layout.reload.left, layout.reload.top, layout.reload.right - layout.reload.left, layout.reload.bottom - layout.reload.top, TRUE);
  MoveWindow(stop_button_, layout.stop.left, layout.stop.top, layout.stop.right - layout.stop.left, layout.stop.bottom - layout.stop.top, TRUE);
  MoveWindow(address_bar_, layout.address.left, layout.address.top, layout.address.right - layout.address.left, layout.address.bottom - layout.address.top, TRUE);
}

void BrowserWindow::ResizeBrowserHost() {
  if (!controller_.has_browser()) {
    return;
  }

  RECT client_rect{};
  GetClientRect(hwnd_, &client_rect);
  const auto layout = win32::ComputeLayout(client_rect);

  const HWND browser_window = controller_.browser()->GetHost()->GetWindowHandle();
  if (browser_window != nullptr) {
    SetWindowPos(browser_window,
                 nullptr,
                 layout.browser.left,
                 layout.browser.top,
                 layout.browser.right - layout.browser.left,
                 layout.browser.bottom - layout.browser.top,
                 SWP_NOZORDER);
  }
}

void BrowserWindow::NavigateFromAddressBar() {
  const std::wstring raw_input = LoadTextFromWindow(address_bar_);
  const std::wstring url = win32::NormalizeAddressInput(raw_input);
  if (url.empty()) {
    return;
  }

  if (!first_navigation_requested_ && metrics_ != nullptr) {
    metrics_->Mark("first_navigation.requested");
    first_navigation_requested_ = true;
  }
  controller_.Navigate(url);
}

void BrowserWindow::UpdateNavigationButtons() {
  EnableWindow(back_button_, controller_.can_go_back());
  EnableWindow(forward_button_, controller_.can_go_forward());
  EnableWindow(stop_button_, controller_.is_loading());
}

void BrowserWindow::SetAddressBarText(const std::wstring& text) {
  if (address_bar_ != nullptr) {
    SetWindowTextW(address_bar_, text.c_str());
  }
}

void BrowserWindow::PostWindowMessage(UINT message) {
  if (hwnd_ != nullptr) {
    PostMessageW(hwnd_, message, 0, 0);
  }
}

void BrowserWindow::HandleBrowserCreatedMessage() {
  CefRefPtr<CefBrowser> browser;
  {
    std::scoped_lock lock(pending_mutex_);
    browser = pending_browser_;
    pending_browser_ = nullptr;
  }
  controller_.SetBrowser(browser);
  ResizeBrowserHost();
  UpdateNavigationButtons();
}

void BrowserWindow::HandleBrowserClosedMessage() {
  controller_.Reset();
  if (hwnd_ != nullptr) {
    DestroyWindow(hwnd_);
  }
}

void BrowserWindow::HandleAddressChangedMessage() {
  std::wstring address;
  {
    std::scoped_lock lock(pending_mutex_);
    address.swap(pending_address_);
  }
  SetAddressBarText(address);
}

void BrowserWindow::HandleTitleChangedMessage() {
  std::wstring title;
  {
    std::scoped_lock lock(pending_mutex_);
    title.swap(pending_title_);
  }
  if (title.empty()) {
    title = L"Velox";
  } else {
    title += L" - Velox";
  }
  SetWindowTextW(hwnd_, title.c_str());
}

void BrowserWindow::HandleLoadingStateMessage() {
  PendingLoadState state;
  {
    std::scoped_lock lock(pending_mutex_);
    state = pending_load_state_;
  }
  controller_.SetLoadingState(state.is_loading, state.can_go_back, state.can_go_forward);
  UpdateNavigationButtons();

  if (state.is_loading) {
    saw_loading_activity_ = true;
  }

  if (!state.is_loading && saw_loading_activity_ && command_line_.quit_after_load && !quit_after_load_posted_) {
    quit_after_load_posted_ = true;
    PostMessageW(hwnd_, WM_CLOSE, 0, 0);
  }
}

void BrowserWindow::HandleLoadErrorMessage() {
  std::wstring failed_url;
  std::wstring error_text;
  {
    std::scoped_lock lock(pending_mutex_);
    failed_url.swap(pending_failed_url_);
    error_text.swap(pending_error_text_);
  }

  if (!failed_url.empty()) {
    SetAddressBarText(failed_url);
  }
  platform::LogWarning("Browser load failed: " + platform::ToUtf8(error_text));
}

}  // namespace velox::browser
