#include "browser/browser_window.h"

#include <commctrl.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <utility>

#include "browser/win32_controls.h"
#include "cef/request_context_factory.h"
#include "include/cef_parser.h"
#include "platform/win/file_utils.h"
#include "platform/win/logger.h"
#include "settings/settings_loader.h"

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
constexpr UINT kMessageOpenTab = WM_APP + 10;
constexpr int kMinWindowWidth = 1120;
constexpr int kMinWindowHeight = 760;

constexpr COLORREF kToolbarColor = RGB(248, 243, 234);
constexpr COLORREF kToolbarBorderColor = RGB(220, 208, 191);
constexpr COLORREF kWindowColor = RGB(255, 252, 247);
constexpr COLORREF kPrimaryTextColor = RGB(31, 28, 25);
constexpr COLORREF kSecondaryTextColor = RGB(110, 101, 91);
constexpr COLORREF kAddressFillColor = RGB(255, 251, 245);
constexpr COLORREF kAddressBorderColor = RGB(209, 191, 169);
constexpr COLORREF kAddressTextColor = RGB(43, 37, 31);
constexpr COLORREF kNavFillColor = RGB(255, 249, 241);
constexpr COLORREF kNavHoverFillColor = RGB(245, 231, 212);
constexpr COLORREF kNavBorderColor = RGB(214, 198, 177);
constexpr COLORREF kNavPressedColor = RGB(232, 214, 192);
constexpr COLORREF kProfileFillColor = RGB(233, 239, 251);
constexpr COLORREF kProfileBorderColor = RGB(159, 180, 220);
constexpr COLORREF kProfileTextColor = RGB(34, 63, 119);
constexpr COLORREF kPrivacyFillColor = RGB(228, 244, 234);
constexpr COLORREF kPrivacyBorderColor = RGB(144, 188, 158);
constexpr COLORREF kPrivacyTextColor = RGB(34, 92, 58);
constexpr COLORREF kTabFillColor = RGB(243, 235, 223);
constexpr COLORREF kTabActiveFillColor = RGB(255, 251, 245);
constexpr COLORREF kTabBorderColor = RGB(214, 198, 177);
constexpr COLORREF kTabActiveBorderColor = RGB(176, 147, 112);
constexpr COLORREF kNewTabFillColor = RGB(236, 225, 209);
constexpr COLORREF kProgressFillColor = RGB(198, 128, 59);
constexpr COLORREF kSettingsPanelFillColor = RGB(255, 250, 243);
constexpr COLORREF kSettingsPanelBorderColor = RGB(198, 181, 156);
constexpr COLORREF kSettingsLabelColor = RGB(92, 82, 70);
constexpr COLORREF kSettingsOptionFillColor = RGB(244, 236, 225);
constexpr COLORREF kSettingsOptionActiveFillColor = RGB(218, 229, 250);
constexpr COLORREF kSettingsOptionActiveBorderColor = RGB(114, 146, 204);
constexpr COLORREF kSettingsOptionActiveTextColor = RGB(26, 56, 112);
constexpr COLORREF kPanelFillColor = RGB(255, 248, 238);
constexpr COLORREF kPanelBorderColor = RGB(207, 188, 160);
constexpr COLORREF kPanelTabFillColor = RGB(242, 232, 217);
constexpr COLORREF kPanelTabActiveFillColor = RGB(227, 238, 252);
constexpr COLORREF kPanelTabActiveBorderColor = RGB(117, 145, 191);
constexpr COLORREF kPanelMutedTextColor = RGB(117, 107, 93);
constexpr int kPanelWidth = 348;
constexpr size_t kMaxHistoryEntries = 120;

struct LoadingStatePayload {
  bool is_loading = false;
  bool can_go_back = false;
  bool can_go_forward = false;
};

struct LoadErrorPayload {
  std::wstring failed_url;
  std::wstring error_text;
};

struct OpenTabPayload {
  std::wstring url;
  bool activate = true;
};

struct SearchProviderPreset {
  const wchar_t* key;
  const wchar_t* label;
  const wchar_t* provider_name;
  const wchar_t* query_url_template;
};

constexpr std::array<SearchProviderPreset, 4> kSearchProviderPresets = {{
    {L"google", L"Google", L"Google", L"https://www.google.com/search?q={query}"},
    {L"duckduckgo", L"DuckDuckGo", L"DuckDuckGo", L"https://duckduckgo.com/?q={query}"},
    {L"bing", L"Bing", L"Bing", L"https://www.bing.com/search?q={query}"},
    {L"startpage", L"Startpage", L"Startpage", L"https://www.startpage.com/do/dsearch?query={query}"},
}};

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

void DrawRoundedBlock(HDC device_context,
                      const RECT& rect,
                      COLORREF fill,
                      COLORREF border,
                      int ellipse,
                      bool fill_only = false) {
  HBRUSH fill_brush = CreateSolidBrush(fill);
  HPEN border_pen = CreatePen(PS_SOLID, 1, fill_only ? fill : border);
  HGDIOBJ previous_brush = SelectObject(device_context, fill_brush);
  HGDIOBJ previous_pen = SelectObject(device_context, border_pen);
  RoundRect(device_context, rect.left, rect.top, rect.right, rect.bottom, ellipse, ellipse);
  SelectObject(device_context, previous_pen);
  SelectObject(device_context, previous_brush);
  DeleteObject(fill_brush);
  DeleteObject(border_pen);
}

void DrawTextBlock(HDC device_context,
                   HFONT font,
                   COLORREF color,
                   const RECT& rect,
                   const std::wstring& text,
                   UINT format) {
  SetBkMode(device_context, TRANSPARENT);
  SetTextColor(device_context, color);
  HGDIOBJ previous_font = SelectObject(device_context, font);
  RECT mutable_rect = rect;
  DrawTextW(device_context, text.c_str(), -1, &mutable_rect, format);
  SelectObject(device_context, previous_font);
}

bool ContainsPoint(const RECT& rect, POINT point) {
  return point.x >= rect.left && point.x < rect.right && point.y >= rect.top && point.y < rect.bottom;
}

RECT MakeRect(int left, int top, int right, int bottom) {
  RECT rect{left, top, right, bottom};
  return rect;
}

const SearchProviderPreset* FindSearchPresetByKey(std::wstring_view key) {
  const auto it = std::find_if(kSearchProviderPresets.begin(),
                               kSearchProviderPresets.end(),
                               [key](const SearchProviderPreset& preset) { return key == preset.key; });
  return it == kSearchProviderPresets.end() ? nullptr : &(*it);
}

const SearchProviderPreset* FindSearchPresetByTemplate(std::wstring_view query_url_template) {
  const auto it = std::find_if(
      kSearchProviderPresets.begin(), kSearchProviderPresets.end(), [query_url_template](const SearchProviderPreset& preset) {
        return query_url_template == preset.query_url_template;
      });
  return it == kSearchProviderPresets.end() ? nullptr : &(*it);
}

std::wstring EscapeField(std::wstring value) {
  std::wstring escaped;
  escaped.reserve(value.size() + 8);
  for (wchar_t ch : value) {
    switch (ch) {
      case L'\\':
        escaped += L"\\\\";
        break;
      case L'\t':
        escaped += L"\\t";
        break;
      case L'\n':
        escaped += L"\\n";
        break;
      case L'\r':
        escaped += L"\\r";
        break;
      default:
        escaped.push_back(ch);
        break;
    }
  }
  return escaped;
}

std::wstring UnescapeField(const std::wstring& value) {
  std::wstring result;
  result.reserve(value.size());
  bool escaping = false;
  for (wchar_t ch : value) {
    if (!escaping) {
      if (ch == L'\\') {
        escaping = true;
      } else {
        result.push_back(ch);
      }
      continue;
    }

    switch (ch) {
      case L't':
        result.push_back(L'\t');
        break;
      case L'n':
        result.push_back(L'\n');
        break;
      case L'r':
        result.push_back(L'\r');
        break;
      default:
        result.push_back(ch);
        break;
    }
    escaping = false;
  }
  return result;
}

std::wstring CurrentTimestampLabel() {
  SYSTEMTIME local_time{};
  GetLocalTime(&local_time);
  wchar_t buffer[64]{};
  swprintf_s(buffer,
             L"%04d-%02d-%02d %02d:%02d",
             local_time.wYear,
             local_time.wMonth,
             local_time.wDay,
             local_time.wHour,
             local_time.wMinute);
  return buffer;
}

std::wstring HistoryStoreLine(const std::wstring& visited_at, const std::wstring& url, const std::wstring& title, const std::wstring& host) {
  return EscapeField(visited_at) + L"\t" + EscapeField(url) + L"\t" + EscapeField(title) + L"\t" + EscapeField(host);
}

std::optional<std::array<std::wstring, 4>> ParseHistoryStoreLine(const std::wstring& line) {
  std::array<std::wstring, 4> fields{};
  size_t field_index = 0;
  size_t start = 0;
  while (start <= line.size() && field_index < fields.size()) {
    const size_t end = line.find(L'\t', start);
    fields[field_index++] = UnescapeField(line.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start));
    if (end == std::wstring::npos) {
      break;
    }
    start = end + 1;
  }
  return field_index == fields.size() ? std::optional(fields) : std::nullopt;
}

std::wstring SanitizeFileName(std::wstring value) {
  for (wchar_t& ch : value) {
    switch (ch) {
      case L'\\':
      case L'/':
      case L':':
      case L'*':
      case L'?':
      case L'"':
      case L'<':
      case L'>':
      case L'|':
        ch = L'_';
        break;
      default:
        break;
    }
  }
  if (value.empty()) {
    return L"download.bin";
  }
  return value;
}

std::wstring PanelModeLabel(BrowserWindow::PanelMode mode) {
  switch (mode) {
    case BrowserWindow::PanelMode::kSettings:
      return L"Settings";
    case BrowserWindow::PanelMode::kHistory:
      return L"History";
    case BrowserWindow::PanelMode::kDownloads:
      return L"Downloads";
    case BrowserWindow::PanelMode::kNone:
      break;
  }
  return L"Panel";
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
      site_predictor_(site_predictor) {
  InitializeTabGroups();
  LoadHistoryFromDisk();
}

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

  request_context_ = cef::CreateRequestContext(settings_, settings_.incognito_default, metrics_);
  SetAddressBarText(settings_.startup_url);
  return CreateTab(settings_.startup_url, true, tab_groups_.empty() ? 0 : tab_groups_.front().id);
}

HWND BrowserWindow::hwnd() const {
  return hwnd_;
}

void BrowserWindow::OnBrowserCreated(int tab_id, CefRefPtr<CefBrowser> browser) {
  if (hwnd_ != nullptr) {
    auto payload = new CefRefPtr<CefBrowser>(browser);
    PostMessageW(hwnd_, kMessageBrowserCreated, static_cast<WPARAM>(tab_id), reinterpret_cast<LPARAM>(payload));
  }
}

void BrowserWindow::OnBrowserClosed(int tab_id) {
  if (hwnd_ != nullptr) {
    PostMessageW(hwnd_, kMessageBrowserClosed, static_cast<WPARAM>(tab_id), 0);
  }
}

void BrowserWindow::OnAddressChanged(int tab_id, const std::wstring& url) {
  PostAddressChangedMessage(tab_id, url);
}

void BrowserWindow::OnTitleChanged(int tab_id, const std::wstring& title) {
  PostTitleChangedMessage(tab_id, title);
}

void BrowserWindow::OnLoadingStateChange(int tab_id, bool is_loading, bool can_go_back, bool can_go_forward) {
  PostLoadingStateMessage(tab_id, is_loading, can_go_back, can_go_forward);
}

void BrowserWindow::OnLoadError(int tab_id, const std::wstring& failed_url, const std::wstring& error_text) {
  PostLoadErrorMessage(tab_id, failed_url, error_text);
}

void BrowserWindow::OnStatusMessage(int tab_id, const std::wstring& status) {
  PostStatusMessage(tab_id, status);
}

void BrowserWindow::OnLoadProgress(int tab_id, double progress) {
  PostLoadProgressMessage(tab_id, static_cast<int>(std::clamp(progress, 0.0, 1.0) * 100.0));
}

void BrowserWindow::OnRendererMetric(int tab_id, const std::string& name, double value) {
  const std::wstring status = FormatRendererStatus(name, value);
  if (!status.empty()) {
    PostRendererMetricMessage(tab_id, status);
  }
}

void BrowserWindow::OnOpenUrlInNewTab(const std::wstring& url, bool activate) {
  PostOpenTabMessage(url, activate);
}

std::wstring BrowserWindow::GetDownloadTargetPath(int tab_id, const std::wstring& suggested_name) {
  (void)tab_id;
  const auto downloads_dir = DownloadsDir();
  platform::EnsureDirectory(downloads_dir);

  std::filesystem::path candidate = downloads_dir / SanitizeFileName(suggested_name);
  const std::filesystem::path stem = candidate.stem();
  const std::filesystem::path extension = candidate.extension();

  int suffix = 2;
  while (std::filesystem::exists(candidate)) {
    candidate = downloads_dir / (stem.wstring() + L" (" + std::to_wstring(suffix++) + L")" + extension.wstring());
  }
  return candidate.wstring();
}

void BrowserWindow::OnDownloadCreated(int tab_id, int download_id, const std::wstring& file_name, const std::wstring& full_path) {
  (void)tab_id;
  auto* existing = FindDownload(download_id);
  if (existing != nullptr) {
    existing->file_name = file_name;
    existing->full_path = full_path;
    existing->status_text = L"Starting download";
    existing->percent_complete = 0;
    existing->is_complete = false;
    existing->is_canceled = false;
  } else {
    download_entries_.insert(download_entries_.begin(),
                             DownloadEntry{download_id, file_name, full_path, L"Starting download", 0, false, false});
  }
  OpenPanel(PanelMode::kDownloads);
  SetStatusText(L"Downloading " + file_name);
}

void BrowserWindow::OnDownloadUpdated(int tab_id,
                                      int download_id,
                                      int percent_complete,
                                      bool is_complete,
                                      bool is_canceled,
                                      const std::wstring& status_text) {
  (void)tab_id;
  auto* download = FindDownload(download_id);
  if (download == nullptr) {
    return;
  }

  download->percent_complete = percent_complete;
  download->is_complete = is_complete;
  download->is_canceled = is_canceled;
  download->status_text = status_text;
  if (panel_mode_ == PanelMode::kDownloads) {
    InvalidateRect(hwnd_, nullptr, FALSE);
  }
  if (is_complete) {
    SetStatusText(L"Finished download: " + download->file_name);
  } else if (is_canceled) {
    SetStatusText(L"Canceled download: " + download->file_name);
  }
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
          if (notification_code == BN_CLICKED) {
            controller_.GoBack();
          }
          return 0;
        case win32::kForwardButtonId:
          if (notification_code == BN_CLICKED) {
            controller_.GoForward();
          }
          return 0;
        case win32::kReloadButtonId:
          if (notification_code == BN_CLICKED) {
            controller_.Reload();
          }
          return 0;
        case win32::kStopButtonId:
          if (notification_code == BN_CLICKED) {
            controller_.Stop();
          }
          return 0;
        case win32::kHomeButtonId:
          if (notification_code == BN_CLICKED && active_tab() != nullptr) {
            controller_.Navigate(settings_.startup_url);
            SetStatusText(L"Returning home...");
          }
          return 0;
        case win32::kProfileBadgeId:
          if (notification_code == BN_CLICKED) {
            ToggleSettingsPanel();
          }
          return 0;
        case win32::kPrivacyBadgeId:
          if (notification_code == BN_CLICKED) {
            SetStatusText(L"Shield on | trackers filtered | third-party cookies blocked");
            settings_panel_open_ = false;
            InvalidateRect(hwnd_, nullptr, FALSE);
          }
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
      SetBkColor(device_context, kAddressFillColor);
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
      DrawCustomChrome(device_context, client_rect);
      EndPaint(hwnd_, &paint_struct);
      return 0;
    }
    case WM_SIZE:
      LayoutChildren();
      ResizeBrowserHosts();
      InvalidateRect(hwnd_, nullptr, FALSE);
      return 0;
    case WM_GETMINMAXINFO: {
      auto* minmax_info = reinterpret_cast<MINMAXINFO*>(lparam);
      minmax_info->ptMinTrackSize.x = kMinWindowWidth;
      minmax_info->ptMinTrackSize.y = kMinWindowHeight;
      return 0;
    }
    case WM_LBUTTONDOWN: {
      POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      OnChromeClick(point);
      return 0;
    }
    case WM_MOUSEMOVE: {
      POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      OnChromeDragMove(point);
      return 0;
    }
    case WM_LBUTTONUP: {
      POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      FinishChromeInteraction(point);
      return 0;
    }
    case WM_CAPTURECHANGED:
      drag_tracking_ = false;
      drag_reordering_ = false;
      drag_tab_id_ = 0;
      drag_target_index_ = -1;
      return 0;
    case WM_CLOSE:
      CloseAllTabsAndWindow();
      return 0;
    case WM_DESTROY:
      DestroyThemeResources();
      hwnd_ = nullptr;
      PostQuitMessage(0);
      return 0;
    case kMessageBrowserCreated: {
      std::unique_ptr<CefRefPtr<CefBrowser>> browser(reinterpret_cast<CefRefPtr<CefBrowser>*>(lparam));
      HandleBrowserCreatedMessage(static_cast<int>(wparam), browser == nullptr ? nullptr : *browser);
      return 0;
    }
    case kMessageBrowserClosed:
      HandleBrowserClosedMessage(static_cast<int>(wparam));
      return 0;
    case kMessageAddressChanged: {
      std::unique_ptr<std::wstring> address(reinterpret_cast<std::wstring*>(lparam));
      HandleAddressChangedMessage(static_cast<int>(wparam), address == nullptr ? std::wstring{} : std::move(*address));
      return 0;
    }
    case kMessageTitleChanged: {
      std::unique_ptr<std::wstring> title(reinterpret_cast<std::wstring*>(lparam));
      HandleTitleChangedMessage(static_cast<int>(wparam), title == nullptr ? std::wstring{} : std::move(*title));
      return 0;
    }
    case kMessageLoadingState: {
      std::unique_ptr<LoadingStatePayload> state(reinterpret_cast<LoadingStatePayload*>(lparam));
      if (state != nullptr) {
        HandleLoadingStateMessage(static_cast<int>(wparam), state->is_loading, state->can_go_back, state->can_go_forward);
      }
      return 0;
    }
    case kMessageLoadError: {
      std::unique_ptr<LoadErrorPayload> payload(reinterpret_cast<LoadErrorPayload*>(lparam));
      if (payload != nullptr) {
        HandleLoadErrorMessage(static_cast<int>(wparam), std::move(payload->failed_url), std::move(payload->error_text));
      }
      return 0;
    }
    case kMessageStatusChanged: {
      std::unique_ptr<std::wstring> status(reinterpret_cast<std::wstring*>(lparam));
      HandleStatusMessage(static_cast<int>(wparam), status == nullptr ? std::wstring{} : std::move(*status));
      return 0;
    }
    case kMessageLoadProgress:
      HandleLoadProgressMessage(static_cast<int>(wparam), static_cast<int>(lparam));
      return 0;
    case kMessageBrowserCommand:
      HandleBrowserCommand(static_cast<cef::BrowserCommand>(wparam));
      return 0;
    case kMessageOpenTab: {
      std::unique_ptr<OpenTabPayload> payload(reinterpret_cast<OpenTabPayload*>(lparam));
      if (payload != nullptr) {
        HandleOpenTabMessage(std::move(payload->url), payload->activate);
      }
      return 0;
    }
  }

  return DefWindowProcW(hwnd_, message, wparam, lparam);
}

void BrowserWindow::CreateControls() {
  brand_label_ = CreateWindowExW(0, L"STATIC", L"Velox", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, hwnd_,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(win32::kBrandLabelId)), instance_, nullptr);
  back_button_ = CreateWindowExW(0, L"BUTTON", L"<", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, hwnd_,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(win32::kBackButtonId)), instance_, nullptr);
  forward_button_ = CreateWindowExW(0, L"BUTTON", L">", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, hwnd_,
                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(win32::kForwardButtonId)), instance_, nullptr);
  reload_button_ = CreateWindowExW(0, L"BUTTON", L"Reload", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, hwnd_,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(win32::kReloadButtonId)), instance_, nullptr);
  stop_button_ = CreateWindowExW(0, L"BUTTON", L"Stop", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, hwnd_,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(win32::kStopButtonId)), instance_, nullptr);
  home_button_ = CreateWindowExW(0, L"BUTTON", L"Home", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, hwnd_,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(win32::kHomeButtonId)), instance_, nullptr);
  address_bar_ = CreateWindowExW(0,
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
  UpdateAddressCueText();
  SendMessageW(address_bar_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(6, 6));
  SendMessageW(progress_bar_, PBM_SETRANGE32, 0, 100);
  SendMessageW(progress_bar_, PBM_SETBKCOLOR, 0, static_cast<LPARAM>(RGB(237, 230, 219)));
  SendMessageW(progress_bar_, PBM_SETBARCOLOR, 0, static_cast<LPARAM>(kProgressFillColor));

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
  RebuildChromeRects(client_rect);

  MoveWindow(brand_label_, layout.brand.left, layout.brand.top, layout.brand.right - layout.brand.left,
             layout.brand.bottom - layout.brand.top, TRUE);
  MoveWindow(back_button_, layout.back.left, layout.back.top, layout.back.right - layout.back.left, layout.back.bottom - layout.back.top, TRUE);
  MoveWindow(forward_button_, layout.forward.left, layout.forward.top, layout.forward.right - layout.forward.left,
             layout.forward.bottom - layout.forward.top, TRUE);
  MoveWindow(reload_button_, layout.reload.left, layout.reload.top, layout.reload.right - layout.reload.left,
             layout.reload.bottom - layout.reload.top, TRUE);
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

void BrowserWindow::ResizeBrowserHosts() {
  if (hwnd_ == nullptr) {
    return;
  }

  RECT client_rect{};
  GetClientRect(hwnd_, &client_rect);
  const auto layout = win32::ComputeLayout(client_rect);
  const int browser_width = IsPanelOpen()
                                ? std::max<int>(320, static_cast<int>(settings_panel_rect_.left - layout.browser.left - 12))
                                : static_cast<int>(layout.browser.right - layout.browser.left);

  for (auto& tab : tabs_) {
    if (tab.browser == nullptr) {
      continue;
    }
    HWND browser_window = tab.browser->GetHost()->GetWindowHandle();
    if (browser_window == nullptr) {
      continue;
    }
    SetWindowPos(browser_window,
                 nullptr,
                 layout.browser.left,
                 layout.browser.top,
                 browser_width,
                 layout.browser.bottom - layout.browser.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(browser_window, tab.id == active_tab_id_ ? SW_SHOW : SW_HIDE);
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
  if (address_bar_ != nullptr) {
    SetFocus(address_bar_);
    SendMessageW(address_bar_, EM_SETSEL, 0, -1);
  }
}

void BrowserWindow::UpdateNavigationButtons() {
  const TabState* tab = active_tab();
  const bool has_tab = tab != nullptr && tab->browser != nullptr;
  EnableWindow(reload_button_, has_tab);
  EnableWindow(back_button_, has_tab && tab->can_go_back);
  EnableWindow(forward_button_, has_tab && tab->can_go_forward);
  EnableWindow(stop_button_, has_tab && tab->is_loading);
  EnableWindow(home_button_, has_tab);
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
  wcscpy_s(font.lfFaceName, L"Bahnschrift");
  title_font_ = CreateFontIndirectW(&font);

  font.lfHeight = -14;
  font.lfWeight = FW_SEMIBOLD;
  wcscpy_s(font.lfFaceName, L"Segoe UI");
  tab_font_ = CreateFontIndirectW(&font);

  toolbar_brush_ = CreateSolidBrush(kToolbarColor);
  window_brush_ = CreateSolidBrush(kWindowColor);
  address_brush_ = CreateSolidBrush(kAddressFillColor);
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
  if (tab_font_ != nullptr) {
    DeleteObject(tab_font_);
    tab_font_ = nullptr;
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
  set_font(back_button_, ui_font_bold_);
  set_font(forward_button_, ui_font_bold_);
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
    const bool show_progress = load_progress_percent_ > 0;
    ShowWindow(progress_bar_, show_progress ? SW_SHOW : SW_HIDE);
  }
}
std::wstring BrowserWindow::BuildDefaultStatusText() const {
  const TabState* tab = active_tab();
  const TabGroup* group = tab == nullptr ? nullptr : FindGroup(tab->group_id);
  if (tab != nullptr && !tab->url.empty() && tab->url != L"about:blank") {
    std::wstring text = L"Viewing ";
    const std::wstring host = ExtractDisplayHost(tab->url);
    text += host.empty() ? TabTitleForDisplay(*tab) : host;
    if (group != nullptr) {
      text += L" | " + group->label + L" group";
    }
    return text;
  }
  if (settings_.incognito_default) {
    return L"Incognito session ready";
  }
  return L"Ready | Ctrl+T new tab | Ctrl+H history | Ctrl+J downloads";
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
    fill_color = RGB(243, 236, 227);
    border_color = RGB(225, 214, 201);
    text_color = RGB(155, 145, 135);
  } else if (is_pressed) {
    fill_color = kNavPressedColor;
  } else if ((draw_item->itemState & ODS_HOTLIGHT) != 0) {
    fill_color = kNavHoverFillColor;
  }

  DrawRoundedBlock(draw_item->hDC, draw_item->rcItem, fill_color, border_color, is_badge ? 18 : 12);
  RECT text_rect = draw_item->rcItem;
  InflateRect(&text_rect, -4, 0);
  DrawTextBlock(draw_item->hDC,
                is_badge ? ui_font_bold_ : ui_font_,
                text_color,
                text_rect,
                LoadTextFromWindow(draw_item->hwndItem),
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  return TRUE;
}

void BrowserWindow::PostAddressChangedMessage(int tab_id, const std::wstring& address) {
  if (hwnd_ != nullptr) {
    PostMessageW(hwnd_, kMessageAddressChanged, static_cast<WPARAM>(tab_id), reinterpret_cast<LPARAM>(new std::wstring(address)));
  }
}

void BrowserWindow::PostTitleChangedMessage(int tab_id, const std::wstring& title) {
  if (hwnd_ != nullptr) {
    PostMessageW(hwnd_, kMessageTitleChanged, static_cast<WPARAM>(tab_id), reinterpret_cast<LPARAM>(new std::wstring(title)));
  }
}

void BrowserWindow::PostLoadingStateMessage(int tab_id, bool is_loading, bool can_go_back, bool can_go_forward) {
  if (hwnd_ != nullptr) {
    auto payload = new LoadingStatePayload{is_loading, can_go_back, can_go_forward};
    PostMessageW(hwnd_, kMessageLoadingState, static_cast<WPARAM>(tab_id), reinterpret_cast<LPARAM>(payload));
  }
}

void BrowserWindow::PostLoadErrorMessage(int tab_id, const std::wstring& failed_url, const std::wstring& error_text) {
  if (hwnd_ != nullptr) {
    auto payload = new LoadErrorPayload{failed_url, error_text};
    PostMessageW(hwnd_, kMessageLoadError, static_cast<WPARAM>(tab_id), reinterpret_cast<LPARAM>(payload));
  }
}

void BrowserWindow::PostStatusMessage(int tab_id, const std::wstring& status) {
  if (hwnd_ != nullptr) {
    PostMessageW(hwnd_, kMessageStatusChanged, static_cast<WPARAM>(tab_id), reinterpret_cast<LPARAM>(new std::wstring(status)));
  }
}

void BrowserWindow::PostLoadProgressMessage(int tab_id, int progress_percent) {
  if (hwnd_ != nullptr) {
    PostMessageW(hwnd_, kMessageLoadProgress, static_cast<WPARAM>(tab_id), static_cast<LPARAM>(progress_percent));
  }
}

void BrowserWindow::PostRendererMetricMessage(int tab_id, const std::wstring& status) {
  PostStatusMessage(tab_id, status);
}

void BrowserWindow::PostOpenTabMessage(const std::wstring& url, bool activate) {
  if (hwnd_ != nullptr) {
    auto payload = new OpenTabPayload{url, activate};
    PostMessageW(hwnd_, kMessageOpenTab, 0, reinterpret_cast<LPARAM>(payload));
  }
}

void BrowserWindow::HandleBrowserCreatedMessage(int tab_id, CefRefPtr<CefBrowser> browser) {
  TabState* tab = FindTab(tab_id);
  if (tab == nullptr) {
    return;
  }

  tab->browser = browser;
  if (active_tab_id_ == 0) {
    active_tab_id_ = tab_id;
  }

  ResizeBrowserHosts();
  RefreshActiveTabChrome();
  if (tab_id == active_tab_id_ && tab->url == L"about:blank") {
    FocusAddressBar();
  }
}

void BrowserWindow::HandleBrowserClosedMessage(int tab_id) {
  RemoveClosedTab(tab_id);
}

void BrowserWindow::HandleAddressChangedMessage(int tab_id, std::wstring address) {
  TabState* tab = FindTab(tab_id);
  if (tab == nullptr) {
    return;
  }

  tab->url = std::move(address);
  if (tab_id == active_tab_id_) {
    current_host_ = ExtractDisplayHost(tab->url);
    SetAddressBarText(tab->url);
    if (!tab->is_loading) {
      SetStatusText(tab->status_text.empty() ? BuildDefaultStatusText() : tab->status_text);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
  }
}

void BrowserWindow::HandleTitleChangedMessage(int tab_id, std::wstring title) {
  TabState* tab = FindTab(tab_id);
  if (tab == nullptr) {
    return;
  }

  tab->title = title.empty() ? L"New tab" : std::move(title);
  if (tab_id == active_tab_id_) {
    SetWindowTextW(hwnd_, (tab->title + L" - Velox").c_str());
    InvalidateRect(hwnd_, nullptr, FALSE);
  }
}

void BrowserWindow::HandleLoadingStateMessage(int tab_id, bool is_loading, bool can_go_back, bool can_go_forward) {
  TabState* tab = FindTab(tab_id);
  if (tab == nullptr) {
    return;
  }

  tab->is_loading = is_loading;
  tab->can_go_back = can_go_back;
  tab->can_go_forward = can_go_forward;
  if (!is_loading && site_predictor_ != nullptr) {
    const std::wstring host = ExtractDisplayHost(tab->url);
    if (!host.empty()) {
      site_predictor_->RecordNavigation(host);
    }
    RecordHistoryVisit(*tab);
  }

  if (tab_id == active_tab_id_) {
    controller_.SetLoadingState(is_loading, can_go_back, can_go_forward);
    UpdateNavigationButtons();
    if (is_loading) {
      saw_loading_activity_ = true;
      SetStatusText(L"Loading page...");
      if (load_progress_percent_ < 8) {
        UpdateProgressBar(8);
      }
    } else {
      UpdateProgressBar(0);
      SetStatusText(tab->status_text.empty() ? BuildDefaultStatusText() : tab->status_text);
    }
  }

  if (!is_loading && saw_loading_activity_ && command_line_.quit_after_load && !quit_after_load_posted_ && tab_id == active_tab_id_) {
    quit_after_load_posted_ = true;
    PostMessageW(hwnd_, WM_CLOSE, 0, 0);
  }
}

void BrowserWindow::HandleLoadErrorMessage(int tab_id, std::wstring failed_url, std::wstring error_text) {
  TabState* tab = FindTab(tab_id);
  if (tab == nullptr) {
    return;
  }

  tab->url = std::move(failed_url);
  tab->load_error = std::move(error_text);
  if (tab_id == active_tab_id_) {
    SetAddressBarText(tab->url);
    SetStatusText(L"Couldn't load page | " + tab->load_error);
  }
  platform::LogWarning("Browser load failed: " + platform::ToUtf8(tab->load_error));
}

void BrowserWindow::HandleStatusMessage(int tab_id, std::wstring status) {
  TabState* tab = FindTab(tab_id);
  if (tab == nullptr) {
    return;
  }

  tab->status_text = std::move(status);
  if (tab_id == active_tab_id_) {
    SetStatusText(tab->status_text.empty() ? BuildDefaultStatusText() : tab->status_text);
  }
}

void BrowserWindow::HandleLoadProgressMessage(int tab_id, int progress_percent) {
  TabState* tab = FindTab(tab_id);
  if (tab == nullptr) {
    return;
  }

  tab->progress_percent = progress_percent;
  if (tab_id == active_tab_id_) {
    if (tab->is_loading && progress_percent < 8) {
      progress_percent = 8;
    }
    UpdateProgressBar(tab->is_loading ? progress_percent : 0);
  }
}

void BrowserWindow::HandleRendererMetricMessage(int tab_id, std::wstring status) {
  HandleStatusMessage(tab_id, std::move(status));
}

void BrowserWindow::HandleBrowserCommand(cef::BrowserCommand command) {
  switch (command) {
    case cef::BrowserCommand::kFocusAddressBar:
      FocusAddressBar();
      break;
    case cef::BrowserCommand::kNewTab:
      CreateTab(L"about:blank", true, ActiveGroupId());
      FocusAddressBar();
      break;
    case cef::BrowserCommand::kNextTab:
      SwitchTabRelative(1);
      break;
    case cef::BrowserCommand::kPreviousTab:
      SwitchTabRelative(-1);
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
    case cef::BrowserCommand::kCloseTabOrWindow:
      CloseActiveTabOrWindow();
      break;
    case cef::BrowserCommand::kToggleHistory:
      TogglePanel(PanelMode::kHistory);
      break;
    case cef::BrowserCommand::kToggleDownloads:
      TogglePanel(PanelMode::kDownloads);
      break;
    case cef::BrowserCommand::kToggleSettings:
      TogglePanel(PanelMode::kSettings);
      break;
  }
}

void BrowserWindow::HandleOpenTabMessage(std::wstring url, bool activate) {
  if (url.empty()) {
    url = L"about:blank";
  }
  CreateTab(url, activate, ActiveGroupId());
}

void BrowserWindow::InitializeTabGroups() {
  tab_groups_.clear();
  tab_groups_.push_back(TabGroup{1, L"Focus", RGB(195, 124, 46)});
  tab_groups_.push_back(TabGroup{2, L"Build", RGB(68, 120, 214)});
  tab_groups_.push_back(TabGroup{3, L"Chill", RGB(68, 153, 109)});
}

BrowserWindow::TabState* BrowserWindow::FindTab(int tab_id) {
  const auto it = std::find_if(tabs_.begin(), tabs_.end(), [tab_id](const TabState& tab) { return tab.id == tab_id; });
  return it == tabs_.end() ? nullptr : &(*it);
}

const BrowserWindow::TabState* BrowserWindow::FindTab(int tab_id) const {
  const auto it = std::find_if(tabs_.begin(), tabs_.end(), [tab_id](const TabState& tab) { return tab.id == tab_id; });
  return it == tabs_.end() ? nullptr : &(*it);
}

int BrowserWindow::FindTabIndex(int tab_id) const {
  for (size_t index = 0; index < tabs_.size(); ++index) {
    if (tabs_[index].id == tab_id) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

BrowserWindow::TabGroup* BrowserWindow::FindGroup(int group_id) {
  const auto it = std::find_if(tab_groups_.begin(), tab_groups_.end(), [group_id](const TabGroup& group) { return group.id == group_id; });
  return it == tab_groups_.end() ? nullptr : &(*it);
}

const BrowserWindow::TabGroup* BrowserWindow::FindGroup(int group_id) const {
  const auto it = std::find_if(tab_groups_.begin(), tab_groups_.end(), [group_id](const TabGroup& group) { return group.id == group_id; });
  return it == tab_groups_.end() ? nullptr : &(*it);
}

const BrowserWindow::TabState* BrowserWindow::active_tab() const {
  return FindTab(active_tab_id_);
}

BrowserWindow::TabState* BrowserWindow::active_tab() {
  return FindTab(active_tab_id_);
}

bool BrowserWindow::CreateTab(const std::wstring& target_url, bool activate, int preferred_group_id) {
  if (hwnd_ == nullptr || request_context_ == nullptr) {
    return false;
  }

  TabState tab;
  tab.id = next_tab_id_++;
  tab.group_id = preferred_group_id == 0 ? ActiveGroupId() : preferred_group_id;
  tab.url = target_url.empty() ? L"about:blank" : target_url;
  tab.client = new cef::VeloxClient(tab.id, this, metrics_);
  tabs_.push_back(tab);

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

  if (!first_navigation_requested_ && metrics_ != nullptr && tab.url != L"about:blank") {
    metrics_->Mark("first_navigation.requested");
    first_navigation_requested_ = true;
  }

  const bool created = CefBrowserHost::CreateBrowser(window_info,
                                                     tabs_.back().client.get(),
                                                     tab.url,
                                                     browser_settings,
                                                     nullptr,
                                                     request_context_);
  if (!created) {
    tabs_.pop_back();
    return false;
  }

  if (activate || active_tab_id_ == 0) {
    active_tab_id_ = tab.id;
  }
  UpdateNavigationButtons();
  InvalidateRect(hwnd_, nullptr, FALSE);
  return true;
}

void BrowserWindow::ActivateTab(int tab_id) {
  if (FindTab(tab_id) == nullptr) {
    return;
  }

  active_tab_id_ = tab_id;
  ResizeBrowserHosts();
  RefreshActiveTabChrome();
  UpdateNavigationButtons();
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void BrowserWindow::ActivateTabByIndex(int index) {
  if (index < 0 || index >= static_cast<int>(tabs_.size())) {
    return;
  }
  ActivateTab(tabs_[index].id);
}

void BrowserWindow::SwitchTabRelative(int delta) {
  if (tabs_.size() < 2) {
    return;
  }

  const int current_index = std::max(0, FindTabIndex(active_tab_id_));
  const int count = static_cast<int>(tabs_.size());
  int next_index = (current_index + delta) % count;
  if (next_index < 0) {
    next_index += count;
  }
  ActivateTabByIndex(next_index);
}

void BrowserWindow::CloseTab(int tab_id) {
  TabState* tab = FindTab(tab_id);
  if (tab == nullptr) {
    return;
  }

  if (tab->browser != nullptr && !tab->close_requested) {
    tab->close_requested = true;
    tab->browser->GetHost()->CloseBrowser(false);
    return;
  }

  RemoveClosedTab(tab_id);
}

void BrowserWindow::CloseActiveTabOrWindow() {
  if (tabs_.size() > 1 && active_tab() != nullptr) {
    CloseTab(active_tab_id_);
    return;
  }

  CloseAllTabsAndWindow();
}

void BrowserWindow::CloseAllTabsAndWindow() {
  if (close_all_requested_) {
    return;
  }

  close_all_requested_ = true;
  std::vector<int> tab_ids;
  tab_ids.reserve(tabs_.size());
  for (const auto& tab : tabs_) {
    tab_ids.push_back(tab.id);
  }
  for (int tab_id : tab_ids) {
    CloseTab(tab_id);
  }

  if (tabs_.empty() && hwnd_ != nullptr) {
    DestroyWindow(hwnd_);
  }
}
void BrowserWindow::RemoveClosedTab(int tab_id) {
  const int closing_index = FindTabIndex(tab_id);
  if (closing_index < 0) {
    return;
  }

  const bool was_active = active_tab_id_ == tab_id;
  tabs_.erase(tabs_.begin() + closing_index);

  if (tabs_.empty()) {
    controller_.Reset();
    if (hwnd_ != nullptr) {
      DestroyWindow(hwnd_);
    }
    return;
  }

  if (was_active) {
    const int next_index = std::min(closing_index, static_cast<int>(tabs_.size()) - 1);
    active_tab_id_ = tabs_[next_index].id;
  }

  ResizeBrowserHosts();
  RefreshActiveTabChrome();
}

void BrowserWindow::RefreshActiveTabChrome() {
  TabState* tab = active_tab();
  if (tab == nullptr) {
    controller_.Reset();
    SetWindowTextW(hwnd_, L"Velox");
    SetStatusText(BuildDefaultStatusText());
    return;
  }

  current_host_ = ExtractDisplayHost(tab->url);
  SetAddressBarText(tab->url);
  SetWindowTextW(hwnd_, (TabTitleForDisplay(*tab) + L" - Velox").c_str());
  UpdateControllerFromActiveTab();
  SetStatusText(tab->status_text.empty() ? BuildDefaultStatusText() : tab->status_text);
  UpdateProgressBar(tab->is_loading ? std::max(8, tab->progress_percent) : 0);
}

void BrowserWindow::UpdateControllerFromActiveTab() {
  TabState* tab = active_tab();
  if (tab == nullptr || tab->browser == nullptr) {
    controller_.Reset();
    UpdateNavigationButtons();
    return;
  }

  controller_.SetBrowser(tab->browser);
  controller_.SetLoadingState(tab->is_loading, tab->can_go_back, tab->can_go_forward);
  UpdateNavigationButtons();
}

void BrowserWindow::DrawCustomChrome(HDC device_context, const RECT& client_rect) {
  const auto layout = win32::ComputeLayout(client_rect);

  RECT toolbar_rect = client_rect;
  toolbar_rect.bottom = std::min<LONG>(toolbar_rect.bottom, win32::kToolbarHeight);
  FillRect(device_context, &toolbar_rect, toolbar_brush_ != nullptr ? toolbar_brush_ : reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1));

  RECT brand_glow = MakeRect(layout.brand.left - 4, layout.brand.top - 8, layout.brand.right + 60, layout.brand.bottom + 8);
  DrawRoundedBlock(device_context, brand_glow, RGB(240, 231, 214), RGB(240, 231, 214), 18, true);

  HPEN border_pen = CreatePen(PS_SOLID, 1, kToolbarBorderColor);
  HGDIOBJ previous_pen = SelectObject(device_context, border_pen);
  MoveToEx(device_context, toolbar_rect.left, toolbar_rect.bottom - 1, nullptr);
  LineTo(device_context, toolbar_rect.right, toolbar_rect.bottom - 1);
  SelectObject(device_context, previous_pen);
  DeleteObject(border_pen);

  DrawRoundedBlock(device_context, layout.address_shell, kAddressFillColor, kAddressBorderColor, 18);
  DrawGroupStrip(device_context);
  DrawTabStrip(device_context);
  DrawActionStrip(device_context);

  RECT new_button_rect = new_tab_button_rect_;
  DrawRoundedBlock(device_context, new_button_rect, kNewTabFillColor, kTabBorderColor, 16);
  DrawTextBlock(device_context, ui_font_bold_, kPrimaryTextColor, new_button_rect, L"+", DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  RECT eyebrow_rect = MakeRect(layout.brand.left + 2, layout.brand.top - 16, layout.brand.left + 140, layout.brand.top + 2);
  DrawTextBlock(device_context, ui_font_, kPanelMutedTextColor, eyebrow_rect, L"speed shell / chromium core", DT_LEFT | DT_VCENTER | DT_SINGLELINE);

  if (IsPanelOpen()) {
    DrawLibraryPanel(device_context);
  }
}

void BrowserWindow::DrawGroupStrip(HDC device_context) {
  const int active_group_id = ActiveGroupId();
  for (const auto& visual : group_visuals_) {
    const TabGroup* group = FindGroup(visual.group_id);
    if (group == nullptr) {
      continue;
    }

    const bool active = group->id == active_group_id;
    const COLORREF fill = active ? group->accent : RGB(241, 232, 219);
    const COLORREF text_color = active ? RGB(255, 255, 255) : kPrimaryTextColor;
    DrawRoundedBlock(device_context, visual.rect, fill, group->accent, 14, active);
    DrawTextBlock(device_context, ui_font_bold_, text_color, visual.rect, group->label, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }
}

void BrowserWindow::DrawTabStrip(HDC device_context) {
  for (const auto& visual : tab_visuals_) {
    const TabState* tab = FindTab(visual.tab_id);
    if (tab == nullptr) {
      continue;
    }

    const TabGroup* group = FindGroup(tab->group_id);
    const bool active = tab->id == active_tab_id_;
    const COLORREF fill = active ? kTabActiveFillColor : kTabFillColor;
    const COLORREF border = active ? (group == nullptr ? kTabActiveBorderColor : group->accent) : kTabBorderColor;
    DrawRoundedBlock(device_context, visual.tab_rect, fill, border, 18);

    RECT accent_rect = visual.tab_rect;
    accent_rect.bottom = accent_rect.top + 4;
    accent_rect.right -= 8;
    accent_rect.left += 8;
    DrawRoundedBlock(device_context,
                     accent_rect,
                     group == nullptr ? kTabActiveBorderColor : group->accent,
                     group == nullptr ? kTabActiveBorderColor : group->accent,
                     8,
                     true);

    RECT title_rect = visual.tab_rect;
    title_rect.left += 14;
    title_rect.right = visual.close_rect.left - 6;
    DrawTextBlock(device_context,
                  tab_font_ != nullptr ? tab_font_ : ui_font_bold_,
                  kPrimaryTextColor,
                  title_rect,
                  TabTitleForDisplay(*tab),
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    DrawTextBlock(device_context, ui_font_bold_, kSecondaryTextColor, visual.close_rect, L"x", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }
}

void BrowserWindow::DrawActionStrip(HDC device_context) {
  for (const auto& action : action_visuals_) {
    const bool active = panel_mode_ == action.mode && IsPanelOpen();
    DrawRoundedBlock(device_context,
                     action.rect,
                     active ? kPanelTabActiveFillColor : kPanelTabFillColor,
                     active ? kPanelTabActiveBorderColor : kTabBorderColor,
                     14);
    DrawTextBlock(device_context,
                  active ? ui_font_bold_ : ui_font_,
                  active ? kProfileTextColor : kPrimaryTextColor,
                  action.rect,
                  action.label,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }
}

void BrowserWindow::DrawLibraryPanel(HDC device_context) {
  DrawRoundedBlock(device_context, settings_panel_rect_, kPanelFillColor, kPanelBorderColor, 24);

  RECT header_rect = settings_panel_rect_;
  header_rect.left += 18;
  header_rect.top += 14;
  header_rect.right -= 18;
  header_rect.bottom = header_rect.top + 22;
  DrawTextBlock(device_context,
                ui_font_bold_,
                kPrimaryTextColor,
                header_rect,
                PanelModeLabel(panel_mode_),
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);

  RECT subtitle_rect = header_rect;
  subtitle_rect.top = header_rect.bottom + 2;
  subtitle_rect.bottom = subtitle_rect.top + 16;
  DrawTextBlock(device_context,
                ui_font_,
                kPanelMutedTextColor,
                subtitle_rect,
                panel_mode_ == PanelMode::kSettings ? L"runtime, privacy, and search controls"
                                                    : panel_mode_ == PanelMode::kHistory
                                                          ? L"recent places you've opened in Velox"
                                                          : L"active and finished downloads",
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

  DrawSettingsPanel(device_context);
}

void BrowserWindow::DrawSettingsPanel(HDC device_context) {
  RECT tabs_row = MakeRect(settings_panel_rect_.left + 18, settings_panel_rect_.top + 52, settings_panel_rect_.right - 18, settings_panel_rect_.top + 84);
  int tab_left = tabs_row.left;
  for (const auto& action : action_visuals_) {
    RECT tab_rect = MakeRect(tab_left, tabs_row.top, tab_left + 92, tabs_row.bottom);
    const bool active = panel_mode_ == action.mode;
    DrawRoundedBlock(device_context,
                     tab_rect,
                     active ? kPanelTabActiveFillColor : kPanelTabFillColor,
                     active ? kPanelTabActiveBorderColor : kTabBorderColor,
                     14);
    DrawTextBlock(device_context,
                  active ? ui_font_bold_ : ui_font_,
                  active ? kProfileTextColor : kPrimaryTextColor,
                  tab_rect,
                  action.label,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    tab_left = tab_rect.right + 8;
  }

  if (panel_mode_ == PanelMode::kHistory) {
    if (history_entries_.empty()) {
      RECT empty_rect = MakeRect(settings_panel_rect_.left + 18, tabs_row.bottom + 18, settings_panel_rect_.right - 18, tabs_row.bottom + 44);
      DrawTextBlock(device_context,
                    ui_font_,
                    kPanelMutedTextColor,
                    empty_rect,
                    L"No history yet. Start browsing and Velox will keep a local trail here.",
                    DT_LEFT | DT_WORDBREAK);
      return;
    }

    for (const auto& visual : history_visuals_) {
      if (visual.index >= history_entries_.size()) {
        continue;
      }
      const auto& entry = history_entries_[visual.index];
      DrawRoundedBlock(device_context, visual.rect, RGB(248, 241, 231), RGB(220, 205, 183), 16);
      RECT title_rect = visual.rect;
      title_rect.left += 12;
      title_rect.top += 10;
      title_rect.right -= 12;
      title_rect.bottom = title_rect.top + 20;
      DrawTextBlock(device_context,
                    ui_font_bold_,
                    kPrimaryTextColor,
                    title_rect,
                    entry.title.empty() ? entry.host : entry.title,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

      RECT meta_rect = title_rect;
      meta_rect.top = title_rect.bottom + 2;
      meta_rect.bottom = meta_rect.top + 16;
      DrawTextBlock(device_context,
                    ui_font_,
                    kPanelMutedTextColor,
                    meta_rect,
                    entry.host + L"  |  " + entry.visited_at,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    return;
  }

  if (panel_mode_ == PanelMode::kDownloads) {
    if (download_entries_.empty()) {
      RECT empty_rect = MakeRect(settings_panel_rect_.left + 18, tabs_row.bottom + 18, settings_panel_rect_.right - 18, tabs_row.bottom + 44);
      DrawTextBlock(device_context,
                    ui_font_,
                    kPanelMutedTextColor,
                    empty_rect,
                    L"No downloads yet. Files you download in this session will show up here.",
                    DT_LEFT | DT_WORDBREAK);
      return;
    }

    for (const auto& visual : download_visuals_) {
      const auto* download = FindDownload(visual.id);
      if (download == nullptr) {
        continue;
      }

      DrawRoundedBlock(device_context, visual.rect, RGB(248, 241, 231), RGB(220, 205, 183), 16);
      RECT title_rect = visual.rect;
      title_rect.left += 12;
      title_rect.top += 10;
      title_rect.right -= 12;
      title_rect.bottom = title_rect.top + 20;
      DrawTextBlock(device_context,
                    ui_font_bold_,
                    kPrimaryTextColor,
                    title_rect,
                    download->file_name,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

      RECT meta_rect = title_rect;
      meta_rect.top = title_rect.bottom + 2;
      meta_rect.bottom = meta_rect.top + 16;
      DrawTextBlock(device_context,
                    ui_font_,
                    kPanelMutedTextColor,
                    meta_rect,
                    download->status_text,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

      RECT bar_rect = MakeRect(visual.rect.left + 12, visual.rect.bottom - 16, visual.rect.right - 12, visual.rect.bottom - 10);
      DrawRoundedBlock(device_context, bar_rect, RGB(233, 225, 215), RGB(233, 225, 215), 8, true);
      if (!download->is_canceled) {
        RECT fill_rect = bar_rect;
        const int fill_width = std::max<int>(
            8,
            static_cast<int>((bar_rect.right - bar_rect.left) * std::max(download->percent_complete, download->is_complete ? 100 : 0) / 100));
        fill_rect.right = std::min(bar_rect.right, bar_rect.left + fill_width);
        DrawRoundedBlock(device_context, fill_rect, kProgressFillColor, kProgressFillColor, 8, true);
      }
    }
    return;
  }

  RECT card_one = MakeRect(settings_panel_rect_.left + 18, tabs_row.bottom + 16, settings_panel_rect_.left + 162, tabs_row.bottom + 92);
  RECT card_two = MakeRect(card_one.right + 12, card_one.top, settings_panel_rect_.right - 18, card_one.bottom);
  RECT card_three = MakeRect(settings_panel_rect_.left + 18, card_one.bottom + 12, settings_panel_rect_.left + 162, card_one.bottom + 88);
  RECT card_four = MakeRect(card_three.right + 12, card_three.top, settings_panel_rect_.right - 18, card_three.bottom);

  const auto draw_card = [&](const RECT& rect, const std::wstring& label, const std::wstring& value, const std::wstring& helper) {
    DrawRoundedBlock(device_context, rect, RGB(248, 241, 231), RGB(220, 205, 183), 18);
    RECT label_rect = rect;
    label_rect.left += 12;
    label_rect.top += 10;
    label_rect.right -= 12;
    label_rect.bottom = label_rect.top + 18;
    DrawTextBlock(device_context, ui_font_, kPanelMutedTextColor, label_rect, label, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    RECT value_rect = label_rect;
    value_rect.top = label_rect.bottom + 4;
    value_rect.bottom = value_rect.top + 22;
    DrawTextBlock(device_context, ui_font_bold_, kPrimaryTextColor, value_rect, value, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    RECT helper_rect = value_rect;
    helper_rect.top = value_rect.bottom + 2;
    helper_rect.bottom = helper_rect.top + 18;
    DrawTextBlock(device_context, ui_font_, kPanelMutedTextColor, helper_rect, helper, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
  };

  draw_card(card_one,
            L"runtime tier",
            app::RuntimeTierToLabel(runtime_profile_.tier),
            L"auto-selected from RAM + cores");
  draw_card(card_two,
            L"privacy shield",
            settings_.blocking.enabled ? L"On" : L"Off",
            L"ad/tracker filtering + cookie hardening");
  draw_card(card_three,
            L"predictive warmup",
            settings_.optimization.predictive_warmup ? L"Enabled" : L"Off",
            L"warms hot hosts from your local profile");
  draw_card(card_four,
            L"cache budget",
            std::to_wstring(settings_.optimization.max_cache_size_mb) + L" MB",
            L"trimmed on startup to stay snappy");

  RECT search_header = MakeRect(settings_panel_rect_.left + 18, card_three.bottom + 18, settings_panel_rect_.right - 18, card_three.bottom + 36);
  DrawTextBlock(device_context,
                ui_font_bold_,
                kPrimaryTextColor,
                search_header,
                L"Search provider",
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  RECT helper_rect = search_header;
  helper_rect.top = search_header.bottom + 2;
  helper_rect.bottom = helper_rect.top + 16;
  DrawTextBlock(device_context,
                ui_font_,
                kSettingsLabelColor,
                helper_rect,
                L"Changes apply instantly to the omnibox and are saved locally.",
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

  for (const auto& option : settings_options_) {
    const SearchProviderPreset* preset = FindSearchPresetByKey(option.key);
    if (preset == nullptr) {
      continue;
    }

    const bool active = settings_.search.query_url_template == preset->query_url_template;
    DrawRoundedBlock(device_context,
                     option.rect,
                     active ? kSettingsOptionActiveFillColor : kSettingsOptionFillColor,
                     active ? kSettingsOptionActiveBorderColor : kTabBorderColor,
                     14);
    DrawTextBlock(device_context,
                  active ? ui_font_bold_ : ui_font_,
                  active ? kSettingsOptionActiveTextColor : kPrimaryTextColor,
                  option.rect,
                  preset->label,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }

  RECT footer_rect = MakeRect(settings_panel_rect_.left + 18, settings_panel_rect_.bottom - 42, settings_panel_rect_.right - 18, settings_panel_rect_.bottom - 18);
  DrawTextBlock(device_context,
                ui_font_,
                kPanelMutedTextColor,
                footer_rect,
                L"Deep engine/privacy switches come from config and request-context policy, so some changes still belong in settings.json for now.",
                DT_LEFT | DT_WORDBREAK);
}

void BrowserWindow::RebuildChromeRects(const RECT& client_rect) {
  const auto layout = win32::ComputeLayout(client_rect);
  groups_strip_rect_ = layout.groups_strip;
  tabs_strip_rect_ = layout.tabs_strip;
  new_tab_button_rect_ = layout.new_tab_button;
  address_shell_rect_ = layout.address_shell;
  settings_panel_rect_ = MakeRect(std::max<int>(layout.browser.right - kPanelWidth - 12, layout.browser.left + 320),
                                  layout.browser.top + 12,
                                  layout.browser.right - 12,
                                  client_rect.bottom - 12);

  action_visuals_.clear();
  const int action_width = 88;
  const int action_height = groups_strip_rect_.bottom - groups_strip_rect_.top;
  int action_left = new_tab_button_rect_.left - 10 - action_width;
  const std::array<ActionVisual, 3> actions = {{
      {PanelMode::kSettings, L"settings", {}},
      {PanelMode::kHistory, L"history", {}},
      {PanelMode::kDownloads, L"downloads", {}},
  }};
  for (int index = static_cast<int>(actions.size()) - 1; index >= 0; --index) {
    const auto& action = actions[static_cast<size_t>(index)];
    RECT rect = MakeRect(action_left, groups_strip_rect_.top, action_left + action_width, groups_strip_rect_.top + action_height);
    action_visuals_.push_back(ActionVisual{action.mode, action.label, rect});
    action_left -= action_width + 8;
  }
  std::reverse(action_visuals_.begin(), action_visuals_.end());
  const int group_right_limit = action_visuals_.empty() ? groups_strip_rect_.right : (action_visuals_.front().rect.left - 12);

  settings_options_.clear();
  const int option_width = 132;
  const int option_height = 28;
  const int option_left = settings_panel_rect_.left + 18;
  const int option_top = settings_panel_rect_.top + 248;
  const int option_gap = 10;
  for (size_t index = 0; index < kSearchProviderPresets.size(); ++index) {
    const int row = static_cast<int>(index / 2);
    const int column = static_cast<int>(index % 2);
    const int left = option_left + column * (option_width + option_gap);
    const int top = option_top + row * (option_height + 8);
    settings_options_.push_back(SettingsOptionVisual{
        kSearchProviderPresets[index].key,
        MakeRect(left, top, left + option_width, top + option_height),
    });
  }

  group_visuals_.clear();
  int left = groups_strip_rect_.left;
  for (const auto& group : tab_groups_) {
    const int width = std::max(88, static_cast<int>(group.label.size()) * 11 + 34);
    const RECT rect = MakeRect(left,
                               groups_strip_rect_.top,
                               std::min<int>(group_right_limit, left + width),
                               groups_strip_rect_.bottom);
    if (rect.right > group_right_limit) {
      break;
    }
    group_visuals_.push_back(GroupVisual{group.id, rect});
    left = rect.right + 10;
  }

  tab_visuals_.clear();
  if (tabs_.empty()) {
    return;
  }

  int current_left = tabs_strip_rect_.left;
  int previous_group_id = tabs_.front().group_id;
  const int available_width = tabs_strip_rect_.right - tabs_strip_rect_.left;
  const int max_width = 220;
  const int min_width = 144;
  const int base_width = std::clamp((available_width - static_cast<int>(tabs_.size() - 1) * 8) / std::max(1, static_cast<int>(tabs_.size())),
                                    min_width,
                                    max_width);

  for (const auto& tab : tabs_) {
    if (current_left >= tabs_strip_rect_.right - 60) {
      break;
    }

    if (tab.group_id != previous_group_id) {
      current_left += 12;
      previous_group_id = tab.group_id;
    }

    const int tab_right = std::min<int>(tabs_strip_rect_.right, current_left + base_width);
    RECT tab_rect = MakeRect(current_left, tabs_strip_rect_.top, tab_right, tabs_strip_rect_.bottom);
    RECT close_rect = MakeRect(tab_rect.right - 28, tab_rect.top + 8, tab_rect.right - 8, tab_rect.bottom - 8);
    tab_visuals_.push_back(TabVisual{tab.id, tab_rect, close_rect});
    current_left = tab_rect.right + 8;
  }

  history_visuals_.clear();
  int history_top = settings_panel_rect_.top + 96;
  for (size_t index = 0; index < std::min<size_t>(history_entries_.size(), 7); ++index) {
    RECT rect = MakeRect(settings_panel_rect_.left + 18, history_top, settings_panel_rect_.right - 18, history_top + 58);
    history_visuals_.push_back(HistoryVisual{index, rect});
    history_top = rect.bottom + 10;
  }

  download_visuals_.clear();
  int download_top = settings_panel_rect_.top + 96;
  for (size_t index = 0; index < std::min<size_t>(download_entries_.size(), 6); ++index) {
    RECT rect = MakeRect(settings_panel_rect_.left + 18, download_top, settings_panel_rect_.right - 18, download_top + 72);
    download_visuals_.push_back(DownloadVisual{download_entries_[index].id, rect});
    download_top = rect.bottom + 10;
  }
}

void BrowserWindow::OnChromeClick(POINT point) {
  if (IsPanelOpen()) {
    const int tabs_top = settings_panel_rect_.top + 52;
    for (const auto& action : action_visuals_) {
      const RECT tab_rect = MakeRect(settings_panel_rect_.left + 18 + static_cast<int>(&action - action_visuals_.data()) * 100,
                                     tabs_top,
                                     settings_panel_rect_.left + 18 + static_cast<int>(&action - action_visuals_.data()) * 100 + 92,
                                     tabs_top + 32);
      if (ContainsPoint(tab_rect, point)) {
        OpenPanel(action.mode);
        return;
      }
    }

    if (panel_mode_ == PanelMode::kSettings) {
      for (const auto& option : settings_options_) {
        if (ContainsPoint(option.rect, point)) {
          const SearchProviderPreset* preset = FindSearchPresetByKey(option.key);
          if (preset != nullptr) {
            ApplySearchProvider(preset->provider_name, preset->query_url_template);
          }
          return;
        }
      }
    } else if (panel_mode_ == PanelMode::kHistory) {
      for (const auto& visual : history_visuals_) {
        if (ContainsPoint(visual.rect, point)) {
          ActivateHistoryEntry(visual.index);
          return;
        }
      }
    } else if (panel_mode_ == PanelMode::kDownloads) {
      for (const auto& visual : download_visuals_) {
        if (ContainsPoint(visual.rect, point)) {
          const auto* download = FindDownload(visual.id);
          if (download != nullptr) {
            if (download->is_complete) {
              ShellExecuteW(hwnd_, L"open", download->full_path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            } else {
              SetStatusText(download->status_text);
            }
          }
          return;
        }
      }
    }

    if (ContainsPoint(settings_panel_rect_, point)) {
      return;
    }
  }

  if (ContainsPoint(new_tab_button_rect_, point)) {
    panel_mode_ = PanelMode::kNone;
    settings_panel_open_ = false;
    CreateTab(L"about:blank", true, ActiveGroupId());
    FocusAddressBar();
    return;
  }

  for (const auto& action : action_visuals_) {
    if (ContainsPoint(action.rect, point)) {
      TogglePanel(action.mode);
      return;
    }
  }

  for (const auto& visual : group_visuals_) {
    if (ContainsPoint(visual.rect, point)) {
      panel_mode_ = PanelMode::kNone;
      settings_panel_open_ = false;
      AssignActiveTabToGroup(visual.group_id);
      return;
    }
  }

  for (const auto& visual : tab_visuals_) {
    if (ContainsPoint(visual.close_rect, point)) {
      panel_mode_ = PanelMode::kNone;
      settings_panel_open_ = false;
      CloseTab(visual.tab_id);
      return;
    }
    if (ContainsPoint(visual.tab_rect, point)) {
      panel_mode_ = PanelMode::kNone;
      settings_panel_open_ = false;
      ActivateTab(visual.tab_id);
      drag_tracking_ = true;
      drag_reordering_ = false;
      drag_tab_id_ = visual.tab_id;
      drag_target_index_ = FindTabIndex(visual.tab_id);
      drag_start_point_ = point;
      SetCapture(hwnd_);
      return;
    }
  }

  panel_mode_ = PanelMode::kNone;
  settings_panel_open_ = false;
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void BrowserWindow::OnChromeDragMove(POINT point) {
  if (!drag_tracking_ || drag_tab_id_ == 0) {
    return;
  }

  if (!drag_reordering_) {
    const int move_x = std::abs(point.x - drag_start_point_.x);
    const int move_y = std::abs(point.y - drag_start_point_.y);
    if (move_x < 10 && move_y < 8) {
      return;
    }
    drag_reordering_ = true;
  }

  if (tab_visuals_.empty()) {
    return;
  }

  int target_index = static_cast<int>(tab_visuals_.size());
  for (size_t index = 0; index < tab_visuals_.size(); ++index) {
    const auto& visual = tab_visuals_[index];
    const int center = visual.tab_rect.left + ((visual.tab_rect.right - visual.tab_rect.left) / 2);
    if (point.x < center) {
      target_index = static_cast<int>(index);
      break;
    }
  }

  ReorderDraggedTab(target_index);
}

void BrowserWindow::FinishChromeInteraction(POINT point) {
  if (!drag_tracking_) {
    return;
  }

  if (drag_reordering_) {
    OnChromeDragMove(point);
  }

  drag_tracking_ = false;
  drag_reordering_ = false;
  drag_tab_id_ = 0;
  drag_target_index_ = -1;
  if (GetCapture() == hwnd_) {
    ReleaseCapture();
  }
}

int BrowserWindow::ActiveGroupId() const {
  const TabState* tab = active_tab();
  if (tab != nullptr) {
    return tab->group_id;
  }
  return tab_groups_.empty() ? 0 : tab_groups_.front().id;
}

void BrowserWindow::AssignActiveTabToGroup(int group_id) {
  TabState* tab = active_tab();
  if (tab == nullptr) {
    return;
  }

  tab->group_id = group_id;
  SetStatusText(BuildDefaultStatusText());
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void BrowserWindow::OpenPanel(PanelMode mode) {
  panel_mode_ = mode;
  settings_panel_open_ = mode != PanelMode::kNone;
  RECT client_rect{};
  if (hwnd_ != nullptr) {
    GetClientRect(hwnd_, &client_rect);
    RebuildChromeRects(client_rect);
    ResizeBrowserHosts();
    InvalidateRect(hwnd_, nullptr, FALSE);
  }
}

void BrowserWindow::TogglePanel(PanelMode mode) {
  if (panel_mode_ == mode && IsPanelOpen()) {
    panel_mode_ = PanelMode::kNone;
    settings_panel_open_ = false;
  } else {
    panel_mode_ = mode;
    settings_panel_open_ = true;
  }
  RECT client_rect{};
  if (hwnd_ != nullptr) {
    GetClientRect(hwnd_, &client_rect);
    RebuildChromeRects(client_rect);
    ResizeBrowserHosts();
    InvalidateRect(hwnd_, nullptr, FALSE);
  }
}

bool BrowserWindow::IsPanelOpen() const {
  return settings_panel_open_ && panel_mode_ != PanelMode::kNone;
}

void BrowserWindow::ToggleSettingsPanel() {
  TogglePanel(PanelMode::kSettings);
  drag_tracking_ = false;
  drag_reordering_ = false;
  drag_tab_id_ = 0;
  drag_target_index_ = -1;
  if (GetCapture() == hwnd_) {
    ReleaseCapture();
  }
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void BrowserWindow::ApplySearchProvider(const std::wstring& provider_name, const std::wstring& query_template) {
  settings_.search.provider_name = provider_name;
  settings_.search.query_url_template = query_template;
  const bool saved = settings::SaveProfilePreferences(settings_);
  UpdateAddressCueText();
  SetStatusText(saved ? L"Search now uses " + provider_name + L" | saved for next launch"
                      : L"Search now uses " + provider_name + L" | couldn't save preference");
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void BrowserWindow::UpdateAddressCueText() const {
  if (address_bar_ == nullptr) {
    return;
  }

  const std::wstring cue_text = L"Search " + settings_.search.provider_name + L" or enter address";
  SendMessageW(address_bar_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(cue_text.c_str()));
}

void BrowserWindow::RecordHistoryVisit(const TabState& tab) {
  if (tab.url.empty() || tab.url == L"about:blank") {
    return;
  }

  const std::wstring host = ExtractDisplayHost(tab.url);
  if (host.empty()) {
    return;
  }

  const std::wstring title = TabTitleForDisplay(tab);
  if (!history_entries_.empty() && history_entries_.front().url == tab.url && history_entries_.front().title == title) {
    return;
  }

  history_entries_.insert(history_entries_.begin(), HistoryEntry{title, tab.url, host, CurrentTimestampLabel()});
  if (history_entries_.size() > kMaxHistoryEntries) {
    history_entries_.resize(kMaxHistoryEntries);
  }
  SaveHistoryToDisk();

  if (hwnd_ != nullptr) {
    RECT client_rect{};
    GetClientRect(hwnd_, &client_rect);
    RebuildChromeRects(client_rect);
    if (panel_mode_ == PanelMode::kHistory) {
      InvalidateRect(hwnd_, nullptr, FALSE);
    }
  }
}

void BrowserWindow::LoadHistoryFromDisk() {
  history_entries_.clear();
  std::wifstream stream(settings_.paths.profile_dir / L"history.tsv");
  if (!stream.is_open()) {
    return;
  }

  std::wstring line;
  while (std::getline(stream, line)) {
    const auto parsed = ParseHistoryStoreLine(line);
    if (!parsed.has_value()) {
      continue;
    }
    history_entries_.push_back(HistoryEntry{(*parsed)[2], (*parsed)[1], (*parsed)[3], (*parsed)[0]});
    if (history_entries_.size() >= kMaxHistoryEntries) {
      break;
    }
  }
}

void BrowserWindow::SaveHistoryToDisk() const {
  if (!platform::EnsureDirectory(settings_.paths.profile_dir)) {
    return;
  }

  std::wofstream stream(settings_.paths.profile_dir / L"history.tsv", std::ios::trunc);
  if (!stream.is_open()) {
    return;
  }

  for (const auto& entry : history_entries_) {
    stream << HistoryStoreLine(entry.visited_at, entry.url, entry.title, entry.host) << L"\n";
  }
}

void BrowserWindow::ActivateHistoryEntry(size_t index) {
  if (index >= history_entries_.size()) {
    return;
  }
  if (active_tab() == nullptr) {
    return;
  }

  SetAddressBarText(history_entries_[index].url);
  controller_.Navigate(history_entries_[index].url);
  SetStatusText(L"Reopened " + history_entries_[index].host);
  panel_mode_ = PanelMode::kNone;
  settings_panel_open_ = false;
  ResizeBrowserHosts();
  InvalidateRect(hwnd_, nullptr, FALSE);
}

BrowserWindow::DownloadEntry* BrowserWindow::FindDownload(int download_id) {
  const auto it = std::find_if(download_entries_.begin(),
                               download_entries_.end(),
                               [download_id](const DownloadEntry& entry) { return entry.id == download_id; });
  return it == download_entries_.end() ? nullptr : &(*it);
}

const BrowserWindow::DownloadEntry* BrowserWindow::FindDownload(int download_id) const {
  const auto it = std::find_if(download_entries_.begin(),
                               download_entries_.end(),
                               [download_id](const DownloadEntry& entry) { return entry.id == download_id; });
  return it == download_entries_.end() ? nullptr : &(*it);
}

std::filesystem::path BrowserWindow::DownloadsDir() const {
  return settings_.paths.profile_dir / L"downloads";
}

void BrowserWindow::ReorderDraggedTab(int target_index) {
  if (drag_tab_id_ == 0) {
    return;
  }

  const int current_index = FindTabIndex(drag_tab_id_);
  if (current_index < 0 || tabs_.empty()) {
    return;
  }

  target_index = std::clamp(target_index, 0, static_cast<int>(tabs_.size()));
  TabState moving_tab = std::move(tabs_[current_index]);
  tabs_.erase(tabs_.begin() + current_index);
  if (target_index > current_index) {
    --target_index;
  }
  if (current_index == target_index || drag_target_index_ == target_index) {
    tabs_.insert(tabs_.begin() + target_index, std::move(moving_tab));
    return;
  }
  tabs_.insert(tabs_.begin() + target_index, std::move(moving_tab));
  drag_target_index_ = target_index;

  RECT client_rect{};
  GetClientRect(hwnd_, &client_rect);
  RebuildChromeRects(client_rect);
  InvalidateRect(hwnd_, nullptr, FALSE);
}

std::wstring BrowserWindow::TabTitleForDisplay(const TabState& tab) const {
  if (!tab.title.empty() && tab.title != L"about:blank") {
    return tab.title;
  }
  const std::wstring host = ExtractDisplayHost(tab.url);
  if (!host.empty()) {
    return host;
  }
  return L"New tab";
}

}  // namespace velox::browser
