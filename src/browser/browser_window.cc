#include "browser/browser_window.h"

#include <commctrl.h>

#include <algorithm>
#include <sstream>
#include <utility>

#include "browser/win32_controls.h"
#include "cef/request_context_factory.h"
#include "include/cef_parser.h"
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
constexpr UINT kMessageStatusChanged = WM_APP + 7;
constexpr UINT kMessageLoadProgress = WM_APP + 8;
constexpr UINT kMessageBrowserCommand = WM_APP + 9;
constexpr int kMinWindowWidth = 960;
constexpr int kMinWindowHeight = 720;

constexpr COLORREF kToolbarColor = RGB(245, 247, 250);
constexpr COLORREF kToolbarBorderColor = RGB(218, 224, 230);
constexpr COLORREF kWindowColor = RGB(255, 255, 255);
constexpr COLORREF kPrimaryTextColor = RGB(24, 30, 37);
constexpr COLORREF kSecondaryTextColor = RGB(101, 114, 128);
constexpr COLORREF kAddressTextColor = RGB(30, 41, 59);
constexpr COLORREF kNavFillColor = RGB(255, 255, 255);
constexpr COLORREF kNavHoverFillColor = RGB(238, 244, 252);
constexpr COLORREF kNavBorderColor = RGB(205, 214, 224);
constexpr COLORREF kNavPressedColor = RGB(220, 233, 248);
constexpr COLORREF kProfileFillColor = RGB(234, 242, 255);
constexpr COLORREF kProfileBorderColor = RGB(164, 191, 232);
constexpr COLORREF kProfileTextColor = RGB(33, 67, 133);
constexpr COLORREF kPrivacyFillColor = RGB(232, 245, 237);
constexpr COLORREF kPrivacyBorderColor = RGB(152, 196, 169);
constexpr COLORREF kPrivacyTextColor = RGB(32, 98, 58);

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

std::wstring FormatRendererStatus(std::string_view name, double value) {
  std::wostringstream stream;
  const double value_ms = std::max(0.0, value);
  if (name == "first-contentful-paint") {
    stream << L"First contentful paint " << static_cast<int>(value_ms) << L" ms";
    return stream.str();
  }
  if (name == "first-paint") {
    stream << L"First paint " << static_cast<int>(value_ms) << L" ms";
    return stream.str();
  }
  return {};
}

std::wstring ExtractDisplayHost(const std::wstring& url) {
  CefURLParts parts;
  if (!CefParseURL(url, parts)) {
    return {};
  }
  if (parts.host.str == nullptr || parts.host.length == 0) {
    return {};
  }
  return CefString(parts.host.str, parts.host.length).ToWString();
}

}  // namespace

BrowserWindow::BrowserWindow(HINSTANCE instance,
                             settings::AppSettings settings,
                             app::CommandLineOptions command_line,
                             app::RuntimeProfile runtime_profile,
                             profiling::MetricsRecorder* metrics,
                             app::SitePredictor* site_predictor)
    : instance_(instance),
      settings_(std::move(settings)),
      command_line_(std::move(command_line)),
      runtime_profile_(std::move(runtime_profile)),
      metrics_(metrics),
      site_predictor_(site_predictor) {}

bool BrowserWindow::Create() {
  INITCOMMONCONTROLSEX common_controls{};
  common_controls.dwSize = sizeof(common_controls);
  common_controls.dwICC = ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS;
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
  window_class.hbrBackground = nullptr;
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
  // CEF can invoke callbacks off the native UI thread, so state changes are
  // marshaled back through private window messages before touching controls.
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

void BrowserWindow::OnStatusMessage(const std::wstring& status) {
  {
    std::scoped_lock lock(pending_mutex_);
    pending_status_text_ = status;
  }
  PostWindowMessage(kMessageStatusChanged);
}

void BrowserWindow::OnLoadProgress(double progress) {
  {
    std::scoped_lock lock(pending_mutex_);
    pending_progress_percent_ = static_cast<int>(std::clamp(progress, 0.0, 1.0) * 100.0);
  }
  PostWindowMessage(kMessageLoadProgress);
}

void BrowserWindow::OnRendererMetric(const std::string& name, double value) {
  const std::wstring status = FormatRendererStatus(name, value);
  if (status.empty()) {
    return;
  }

  {
    std::scoped_lock lock(pending_mutex_);
    pending_status_text_ = status;
  }
  PostWindowMessage(kMessageStatusChanged);
}

void BrowserWindow::OnBrowserCommand(cef::BrowserCommand command) {
  if (hwnd_ != nullptr) {
    PostMessageW(hwnd_, kMessageBrowserCommand, static_cast<WPARAM>(command), 0);
  }
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
      CreateThemeResources();
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
        case win32::kHomeButtonId:
          controller_.Navigate(settings_.startup_url);
          SetStatusText(L"Returning home...");
          return 0;
        case win32::kAddressBarId:
          if (notification_code == win32::kAddressEnterNotification) {
            NavigateFromAddressBar();
            return 0;
          }
          if (notification_code == EN_SETFOCUS) {
            SendMessageW(address_bar_, EM_SETSEL, 0, -1);
            return 0;
          }
          break;
      }
      break;
    }
    case WM_DRAWITEM:
      return HandleDrawItem(reinterpret_cast<const DRAWITEMSTRUCT*>(lparam));
    case WM_CTLCOLORSTATIC: {
      HDC device_context = reinterpret_cast<HDC>(wparam);
      HWND control = reinterpret_cast<HWND>(lparam);
      SetBkMode(device_context, TRANSPARENT);
      if (control == brand_label_) {
        SetTextColor(device_context, kPrimaryTextColor);
      } else {
        SetTextColor(device_context, kSecondaryTextColor);
      }
      return reinterpret_cast<LRESULT>(toolbar_brush_ != nullptr ? toolbar_brush_ : GetStockObject(WHITE_BRUSH));
    }
    case WM_CTLCOLOREDIT: {
      HDC device_context = reinterpret_cast<HDC>(wparam);
      SetBkColor(device_context, kWindowColor);
      SetTextColor(device_context, kAddressTextColor);
      return reinterpret_cast<LRESULT>(address_brush_ != nullptr ? address_brush_ : GetStockObject(WHITE_BRUSH));
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT paint_struct{};
      HDC device_context = BeginPaint(hwnd_, &paint_struct);
      RECT client_rect{};
      GetClientRect(hwnd_, &client_rect);
      FillRect(device_context, &client_rect, window_brush_ != nullptr ? window_brush_ : reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));

      RECT toolbar_rect = client_rect;
      toolbar_rect.bottom = std::min<LONG>(toolbar_rect.bottom, win32::kToolbarHeight);
      FillRect(device_context, &toolbar_rect, toolbar_brush_ != nullptr ? toolbar_brush_ : reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1));

      HPEN border_pen = CreatePen(PS_SOLID, 1, kToolbarBorderColor);
      HGDIOBJ previous_pen = SelectObject(device_context, border_pen);
      MoveToEx(device_context, toolbar_rect.left, toolbar_rect.bottom - 1, nullptr);
      LineTo(device_context, toolbar_rect.right, toolbar_rect.bottom - 1);
      SelectObject(device_context, previous_pen);
      DeleteObject(border_pen);
      EndPaint(hwnd_, &paint_struct);
      return 0;
    }
    case WM_SIZE:
      LayoutChildren();
      ResizeBrowserHost();
      InvalidateRect(hwnd_, nullptr, FALSE);
      return 0;
    case WM_GETMINMAXINFO: {
      auto* minmax_info = reinterpret_cast<MINMAXINFO*>(lparam);
      minmax_info->ptMinTrackSize.x = kMinWindowWidth;
      minmax_info->ptMinTrackSize.y = kMinWindowHeight;
      return 0;
    }
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
      DestroyThemeResources();
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
    case kMessageStatusChanged:
      HandleStatusMessage();
      return 0;
    case kMessageLoadProgress:
      HandleLoadProgressMessage();
      return 0;
    case kMessageBrowserCommand:
      HandleBrowserCommand(static_cast<cef::BrowserCommand>(wparam));
      return 0;
  }

  return DefWindowProcW(hwnd_, message, wparam, lparam);
}

void BrowserWindow::CreateControls() {
  // Native controls keep the shell startup path cheap while still letting us
  // make the chrome feel intentional instead of placeholder-grade.
  brand_label_ = CreateWindowExW(0, L"STATIC", L"Velox", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, hwnd_,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(win32::kBrandLabelId)), instance_, nullptr);
  back_button_ = CreateWindowExW(0, L"BUTTON", L"Back", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, hwnd_,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(win32::kBackButtonId)), instance_, nullptr);
  forward_button_ = CreateWindowExW(0, L"BUTTON", L"Next", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, hwnd_,
                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(win32::kForwardButtonId)), instance_, nullptr);
  reload_button_ = CreateWindowExW(0, L"BUTTON", L"Reload", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, hwnd_,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(win32::kReloadButtonId)), instance_, nullptr);
  stop_button_ = CreateWindowExW(0, L"BUTTON", L"Stop", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, hwnd_,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(win32::kStopButtonId)), instance_, nullptr);
  home_button_ = CreateWindowExW(0, L"BUTTON", L"Home", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, hwnd_,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(win32::kHomeButtonId)), instance_, nullptr);
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
  profile_badge_ = CreateWindowExW(0, L"BUTTON", BuildProfileBadgeText().c_str(), WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0,
                                   hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(win32::kProfileBadgeId)), instance_, nullptr);
  privacy_badge_ = CreateWindowExW(0, L"BUTTON", BuildPrivacyBadgeText().c_str(), WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0,
                                   hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(win32::kPrivacyBadgeId)), instance_, nullptr);
  status_label_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, hwnd_,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(win32::kStatusLabelId)), instance_, nullptr);
  progress_bar_ = CreateWindowExW(0, PROGRESS_CLASSW, nullptr, WS_CHILD | PBS_SMOOTH, 0, 0, 0, 0, hwnd_,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(win32::kProgressBarId)), instance_, nullptr);
  win32::InstallAddressBarSubclass(address_bar_, hwnd_);
  const std::wstring cue_text = L"Search " + settings_.search.provider_name + L" or enter address";
  SendMessageW(address_bar_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(cue_text.c_str()));
  SendMessageW(address_bar_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(8, 8));
  SendMessageW(progress_bar_, PBM_SETRANGE32, 0, 100);
  SendMessageW(progress_bar_, PBM_SETBKCOLOR, 0, static_cast<LPARAM>(RGB(233, 238, 243)));
  SendMessageW(progress_bar_, PBM_SETBARCOLOR, 0, static_cast<LPARAM>(RGB(58, 123, 213)));
  status_text_ = BuildDefaultStatusText();
  SetStatusText(status_text_);
  ApplyControlFonts();
  ShowWindow(progress_bar_, SW_HIDE);
  UpdateNavigationButtons();
}

void BrowserWindow::LayoutChildren() {
  if (hwnd_ == nullptr) {
    return;
  }

  RECT client_rect{};
  GetClientRect(hwnd_, &client_rect);
  const auto layout = win32::ComputeLayout(client_rect);

  // The toolbar layout is deterministic and allocation-free so resize work
  // stays cheap while Chromium is busy painting below it.
  MoveWindow(brand_label_, layout.brand.left, layout.brand.top, layout.brand.right - layout.brand.left,
             layout.brand.bottom - layout.brand.top, TRUE);
  MoveWindow(back_button_, layout.back.left, layout.back.top, layout.back.right - layout.back.left, layout.back.bottom - layout.back.top, TRUE);
  MoveWindow(forward_button_, layout.forward.left, layout.forward.top, layout.forward.right - layout.forward.left, layout.forward.bottom - layout.forward.top, TRUE);
  MoveWindow(reload_button_, layout.reload.left, layout.reload.top, layout.reload.right - layout.reload.left, layout.reload.bottom - layout.reload.top, TRUE);
  MoveWindow(stop_button_, layout.stop.left, layout.stop.top, layout.stop.right - layout.stop.left, layout.stop.bottom - layout.stop.top, TRUE);
  MoveWindow(home_button_, layout.home.left, layout.home.top, layout.home.right - layout.home.left, layout.home.bottom - layout.home.top, TRUE);
  MoveWindow(address_bar_, layout.address.left, layout.address.top, layout.address.right - layout.address.left, layout.address.bottom - layout.address.top, TRUE);
  MoveWindow(profile_badge_, layout.profile.left, layout.profile.top, layout.profile.right - layout.profile.left,
             layout.profile.bottom - layout.profile.top, TRUE);
  MoveWindow(privacy_badge_, layout.privacy.left, layout.privacy.top, layout.privacy.right - layout.privacy.left,
             layout.privacy.bottom - layout.privacy.top, TRUE);
  MoveWindow(status_label_, layout.status.left, layout.status.top, layout.status.right - layout.status.left,
             layout.status.bottom - layout.status.top, TRUE);
  MoveWindow(progress_bar_, layout.progress.left, layout.progress.top, layout.progress.right - layout.progress.left,
             layout.progress.bottom - layout.progress.top, TRUE);
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
  const std::wstring url = win32::NormalizeAddressInput(raw_input, settings_.search.query_url_template);
  if (url.empty()) {
    return;
  }

  if (!first_navigation_requested_ && metrics_ != nullptr) {
    metrics_->Mark("first_navigation.requested");
    first_navigation_requested_ = true;
  }
  SetStatusText(L"Opening page...");
  UpdateProgressBar(10);
  controller_.Navigate(url);
}

void BrowserWindow::FocusAddressBar() {
  if (address_bar_ == nullptr) {
    return;
  }

  SetFocus(address_bar_);
  SendMessageW(address_bar_, EM_SETSEL, 0, -1);
}

void BrowserWindow::UpdateNavigationButtons() {
  EnableWindow(reload_button_, controller_.has_browser());
  EnableWindow(back_button_, controller_.can_go_back());
  EnableWindow(forward_button_, controller_.can_go_forward());
  EnableWindow(stop_button_, controller_.is_loading());
  EnableWindow(home_button_, controller_.has_browser());
}

void BrowserWindow::CreateThemeResources() {
  LOGFONTW font{};
  font.lfHeight = -15;
  font.lfWeight = FW_NORMAL;
  wcscpy_s(font.lfFaceName, L"Segoe UI");
  ui_font_ = CreateFontIndirectW(&font);

  font.lfWeight = FW_SEMIBOLD;
  ui_font_bold_ = CreateFontIndirectW(&font);

  font.lfHeight = -22;
  title_font_ = CreateFontIndirectW(&font);

  // These GDI objects are created once and reused to avoid per-paint churn.
  toolbar_brush_ = CreateSolidBrush(kToolbarColor);
  window_brush_ = CreateSolidBrush(kWindowColor);
  address_brush_ = CreateSolidBrush(kWindowColor);
}

void BrowserWindow::DestroyThemeResources() {
  if (ui_font_ != nullptr) {
    DeleteObject(ui_font_);
    ui_font_ = nullptr;
  }
  if (ui_font_bold_ != nullptr) {
    DeleteObject(ui_font_bold_);
    ui_font_bold_ = nullptr;
  }
  if (title_font_ != nullptr) {
    DeleteObject(title_font_);
    title_font_ = nullptr;
  }
  if (toolbar_brush_ != nullptr) {
    DeleteObject(toolbar_brush_);
    toolbar_brush_ = nullptr;
  }
  if (window_brush_ != nullptr) {
    DeleteObject(window_brush_);
    window_brush_ = nullptr;
  }
  if (address_brush_ != nullptr) {
    DeleteObject(address_brush_);
    address_brush_ = nullptr;
  }
}

void BrowserWindow::ApplyControlFonts() const {
  const auto set_font = [](HWND control, HFONT font) {
    if (control != nullptr && font != nullptr) {
      SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
  };

  set_font(brand_label_, title_font_);
  set_font(back_button_, ui_font_);
  set_font(forward_button_, ui_font_);
  set_font(reload_button_, ui_font_);
  set_font(stop_button_, ui_font_);
  set_font(home_button_, ui_font_);
  set_font(address_bar_, ui_font_);
  set_font(profile_badge_, ui_font_bold_);
  set_font(privacy_badge_, ui_font_bold_);
  set_font(status_label_, ui_font_);
}

void BrowserWindow::SetAddressBarText(const std::wstring& text) {
  if (address_bar_ != nullptr) {
    SetWindowTextW(address_bar_, text.c_str());
  }
}

void BrowserWindow::SetStatusText(const std::wstring& text) {
  status_text_ = text.empty() ? BuildDefaultStatusText() : text;
  if (status_label_ != nullptr) {
    SetWindowTextW(status_label_, status_text_.c_str());
  }
}

void BrowserWindow::UpdateProgressBar(int progress_percent) {
  load_progress_percent_ = std::clamp(progress_percent, 0, 100);
  if (progress_bar_ != nullptr) {
    SendMessageW(progress_bar_, PBM_SETPOS, load_progress_percent_, 0);
    const bool show_progress = controller_.is_loading() || load_progress_percent_ > 0;
    ShowWindow(progress_bar_, show_progress ? SW_SHOW : SW_HIDE);
  }
}

std::wstring BrowserWindow::BuildDefaultStatusText() const {
  if (!current_host_.empty()) {
    return L"Viewing " + current_host_;
  }
  if (settings_.incognito_default) {
    return L"Incognito session ready";
  }
  return L"Ready | " + settings_.search.provider_name + L" search | Shield on";
}

std::wstring BrowserWindow::BuildPrivacyBadgeText() const {
  return settings_.incognito_default ? L"Incognito" : L"Shield";
}

std::wstring BrowserWindow::BuildProfileBadgeText() const {
  return app::RuntimeTierToLabel(runtime_profile_.tier);
}

LRESULT BrowserWindow::HandleDrawItem(const DRAWITEMSTRUCT* draw_item) {
  if (draw_item == nullptr) {
    return FALSE;
  }

  const int control_id = static_cast<int>(draw_item->CtlID);
  const bool is_profile_badge = control_id == win32::kProfileBadgeId;
  const bool is_privacy_badge = control_id == win32::kPrivacyBadgeId;
  const bool is_badge = is_profile_badge || is_privacy_badge;
  const bool is_pressed = (draw_item->itemState & ODS_SELECTED) != 0;
  const bool is_disabled = (draw_item->itemState & ODS_DISABLED) != 0;

  // Buttons and badges share one lightweight owner-draw path so we can style
  // the chrome without adding another UI framework on top of Win32.
  COLORREF fill_color = kNavFillColor;
  COLORREF border_color = kNavBorderColor;
  COLORREF text_color = kPrimaryTextColor;
  if (is_badge) {
    if (is_profile_badge) {
      fill_color = kProfileFillColor;
      border_color = kProfileBorderColor;
      text_color = kProfileTextColor;
    } else {
      fill_color = kPrivacyFillColor;
      border_color = kPrivacyBorderColor;
      text_color = kPrivacyTextColor;
    }
  } else if (is_disabled) {
    fill_color = RGB(248, 250, 252);
    border_color = RGB(223, 228, 234);
    text_color = RGB(159, 170, 182);
  } else if (is_pressed) {
    fill_color = kNavPressedColor;
  } else if ((draw_item->itemState & ODS_HOTLIGHT) != 0) {
    fill_color = kNavHoverFillColor;
  }

  HBRUSH fill_brush = CreateSolidBrush(fill_color);
  HPEN border_pen = CreatePen(PS_SOLID, 1, border_color);
  HGDIOBJ previous_brush = SelectObject(draw_item->hDC, fill_brush);
  HGDIOBJ previous_pen = SelectObject(draw_item->hDC, border_pen);
  RoundRect(draw_item->hDC,
            draw_item->rcItem.left,
            draw_item->rcItem.top,
            draw_item->rcItem.right,
            draw_item->rcItem.bottom,
            is_badge ? 18 : 12,
            is_badge ? 18 : 12);
  SelectObject(draw_item->hDC, previous_pen);
  SelectObject(draw_item->hDC, previous_brush);
  DeleteObject(border_pen);
  DeleteObject(fill_brush);

  RECT text_rect = draw_item->rcItem;
  InflateRect(&text_rect, -6, 0);
  SetBkMode(draw_item->hDC, TRANSPARENT);
  SetTextColor(draw_item->hDC, text_color);
  HGDIOBJ previous_font =
      SelectObject(draw_item->hDC, is_badge ? static_cast<HGDIOBJ>(ui_font_bold_) : static_cast<HGDIOBJ>(ui_font_));
  const std::wstring text = LoadTextFromWindow(draw_item->hwndItem);
  DrawTextW(draw_item->hDC, text.c_str(), -1, &text_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  SelectObject(draw_item->hDC, previous_font);
  return TRUE;
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
  current_host_ = ExtractDisplayHost(address);
  SetAddressBarText(address);
  if (!controller_.is_loading()) {
    SetStatusText(BuildDefaultStatusText());
  }
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
    SetStatusText(L"Loading page...");
    if (load_progress_percent_ < 8) {
      UpdateProgressBar(8);
    }
  } else {
    if (site_predictor_ != nullptr && !current_host_.empty()) {
      site_predictor_->RecordNavigation(current_host_);
    }
    UpdateProgressBar(0);
    SetStatusText(BuildDefaultStatusText());
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
  SetStatusText(L"Couldn't load page | " + error_text);
  platform::LogWarning("Browser load failed: " + platform::ToUtf8(error_text));
}

void BrowserWindow::HandleStatusMessage() {
  std::wstring status;
  {
    std::scoped_lock lock(pending_mutex_);
    status.swap(pending_status_text_);
  }

  // Renderer paint metrics and Chromium status text can arrive in bursts; the
  // last message wins so the shell never blocks waiting on richer UI state.
  if (status.empty()) {
    if (!controller_.is_loading()) {
      SetStatusText(BuildDefaultStatusText());
    }
    return;
  }
  SetStatusText(status);
}

void BrowserWindow::HandleLoadProgressMessage() {
  int progress_percent = 0;
  {
    std::scoped_lock lock(pending_mutex_);
    progress_percent = pending_progress_percent_;
  }

  if (controller_.is_loading() && progress_percent < 8) {
    progress_percent = 8;
  }

  if (!controller_.is_loading() && progress_percent >= 100) {
    UpdateProgressBar(0);
    return;
  }
  UpdateProgressBar(progress_percent);
}

void BrowserWindow::HandleBrowserCommand(cef::BrowserCommand command) {
  switch (command) {
    case cef::BrowserCommand::kFocusAddressBar:
      FocusAddressBar();
      break;
    case cef::BrowserCommand::kGoBack:
      controller_.GoBack();
      break;
    case cef::BrowserCommand::kGoForward:
      controller_.GoForward();
      break;
    case cef::BrowserCommand::kReload:
      controller_.Reload();
      break;
    case cef::BrowserCommand::kStop:
      controller_.Stop();
      break;
    case cef::BrowserCommand::kCloseWindow:
      if (hwnd_ != nullptr) {
        PostMessageW(hwnd_, WM_CLOSE, 0, 0);
      }
      break;
  }
}

}  // namespace velox::browser

