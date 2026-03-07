#include "browser/win32_controls.h"

#include <commctrl.h>

#include <algorithm>
#include <cwchar>
#include <cwctype>

#include "include/cef_parser.h"

namespace velox::browser::win32 {

namespace {

constexpr int kPadding = 14;
constexpr int kBackButtonWidth = 54;
constexpr int kForwardButtonWidth = 54;
constexpr int kReloadButtonWidth = 72;
constexpr int kStopButtonWidth = 56;
constexpr int kHomeButtonWidth = 62;
constexpr int kControlHeight = 36;
constexpr int kBrandWidth = 96;
constexpr int kBadgeWidth = 96;
constexpr int kStatusHeight = 18;
constexpr int kProgressHeight = 4;
constexpr int kGroupRowHeight = 30;
constexpr int kTabsRowHeight = 36;
constexpr int kToolbarTop = 92;
constexpr int kAddressShellHeight = 42;
constexpr int kNewTabButtonWidth = 42;

std::wstring BuildSearchUrl(std::wstring_view query, std::wstring_view search_url_template) {
  const std::wstring encoded_query = CefURIEncode(std::wstring(query), true).ToWString();
  const size_t token_pos = search_url_template.find(L"{query}");
  if (token_pos == std::wstring::npos) {
    return std::wstring(search_url_template) + encoded_query;
  }

  std::wstring result(search_url_template);
  result.replace(token_pos, wcslen(L"{query}"), encoded_query);
  return result;
}

LRESULT CALLBACK AddressBarSubclassProc(HWND window,
                                        UINT message,
                                        WPARAM wparam,
                                        LPARAM lparam,
                                        UINT_PTR subclass_id,
                                        DWORD_PTR reference_data) {
  (void)subclass_id;
  if (message == WM_KEYDOWN && wparam == VK_RETURN) {
    PostMessageW(reinterpret_cast<HWND>(reference_data),
                 WM_COMMAND,
                 MAKEWPARAM(kAddressBarId, kAddressEnterNotification),
                 reinterpret_cast<LPARAM>(window));
    return 0;
  }
  return DefSubclassProc(window, message, wparam, lparam);
}

}  // namespace

LayoutRects ComputeLayout(const RECT& client_rect) {
  LayoutRects rects;

  const int width = client_rect.right - client_rect.left;
  const int group_top = kPadding;
  const int tabs_top = group_top + kGroupRowHeight + 10;
  const int controls_top = kToolbarTop;
  const int controls_bottom = controls_top + kControlHeight;
  const int status_top = controls_bottom + 10;
  const int progress_top = status_top + kStatusHeight + 8;

  rects.groups_strip = {kPadding, group_top, width - kPadding - kNewTabButtonWidth - kPadding, group_top + kGroupRowHeight};
  rects.new_tab_button = {width - kPadding - kNewTabButtonWidth, group_top, width - kPadding, group_top + kGroupRowHeight};
  rects.tabs_strip = {kPadding, tabs_top, width - kPadding, tabs_top + kTabsRowHeight};

  rects.brand = {kPadding, controls_top, kPadding + kBrandWidth, controls_bottom};
  rects.back = {rects.brand.right + kPadding, controls_top, rects.brand.right + kPadding + kBackButtonWidth, controls_bottom};
  rects.forward = {rects.back.right + 10, controls_top, rects.back.right + 10 + kForwardButtonWidth, controls_bottom};
  rects.reload = {rects.forward.right + 10, controls_top, rects.forward.right + 10 + kReloadButtonWidth, controls_bottom};
  rects.stop = {rects.reload.right + 10, controls_top, rects.reload.right + 10 + kStopButtonWidth, controls_bottom};
  rects.home = {rects.stop.right + 10, controls_top, rects.stop.right + 10 + kHomeButtonWidth, controls_bottom};

  rects.privacy = {width - kPadding - kBadgeWidth, controls_top, width - kPadding, controls_bottom};
  rects.profile = {rects.privacy.left - 10 - kBadgeWidth, controls_top, rects.privacy.left - 10, controls_bottom};

  const LONG address_shell_left = rects.home.right + 12;
  const LONG address_shell_right = std::max<LONG>(address_shell_left + 220, rects.profile.left - 12);
  rects.address_shell = {address_shell_left, controls_top - 3, address_shell_right, controls_top - 3 + kAddressShellHeight};
  rects.address = {rects.address_shell.left + 14,
                   rects.address_shell.top + 8,
                   rects.address_shell.right - 14,
                   rects.address_shell.bottom - 8};

  rects.status = {kPadding, status_top, width - kPadding, status_top + kStatusHeight};
  rects.progress = {kPadding, progress_top, width - kPadding, progress_top + kProgressHeight};
  rects.browser = {0, kToolbarHeight, width, client_rect.bottom};
  return rects;
}

void InstallAddressBarSubclass(HWND address_bar, HWND parent) {
  SetWindowSubclass(address_bar, AddressBarSubclassProc, 1, reinterpret_cast<DWORD_PTR>(parent));
}

std::wstring NormalizeAddressInput(std::wstring value, std::wstring_view search_url_template) {
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](wchar_t ch) { return !iswspace(ch); }));
  value.erase(std::find_if(value.rbegin(), value.rend(), [](wchar_t ch) { return !iswspace(ch); }).base(), value.end());

  if (value.empty()) {
    return value;
  }

  const auto has_whitespace = std::find_if(value.begin(), value.end(), [](wchar_t ch) { return iswspace(ch); }) != value.end();
  const bool has_scheme = value.find(L"://") != std::wstring::npos || value.rfind(L"about:", 0) == 0 ||
                          value.rfind(L"file:", 0) == 0 || value.rfind(L"data:", 0) == 0;
  if (has_scheme) {
    return value;
  }

  const bool looks_like_host =
      value.find(L'.') != std::wstring::npos || value.find(L':') != std::wstring::npos || value == L"localhost";
  if (!has_whitespace && looks_like_host) {
    return L"https://" + value;
  }

  return BuildSearchUrl(value, search_url_template);
}

}  // namespace velox::browser::win32
