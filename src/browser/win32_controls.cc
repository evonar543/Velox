#include "browser/win32_controls.h"

#include <commctrl.h>

#include <algorithm>
#include <cwchar>
#include <cwctype>

#include "include/cef_parser.h"

namespace velox::browser::win32 {

namespace {

constexpr int kPadding = 14;
constexpr int kBackButtonWidth = 62;
constexpr int kForwardButtonWidth = 62;
constexpr int kReloadButtonWidth = 76;
constexpr int kStopButtonWidth = 58;
constexpr int kHomeButtonWidth = 62;
constexpr int kControlHeight = 36;
constexpr int kBrandWidth = 108;
constexpr int kBadgeWidth = 96;
constexpr int kStatusHeight = 18;
constexpr int kProgressHeight = 3;

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
  const int top = kPadding;
  const int row_bottom = top + kControlHeight;
  const int status_top = row_bottom + 10;
  const int progress_top = status_top + kStatusHeight + 8;

  // The toolbar is split into three zones: brand + nav, address field, and
  // compact status badges. Keeping the math flat makes resize behavior cheap.
  rects.brand = {kPadding, top, kPadding + kBrandWidth, row_bottom};
  rects.back = {rects.brand.right + kPadding, top, rects.brand.right + kPadding + kBackButtonWidth, row_bottom};
  rects.forward = {rects.back.right + kPadding, top, rects.back.right + kPadding + kForwardButtonWidth, row_bottom};
  rects.reload = {rects.forward.right + kPadding, top, rects.forward.right + kPadding + kReloadButtonWidth, row_bottom};
  rects.stop = {rects.reload.right + kPadding, top, rects.reload.right + kPadding + kStopButtonWidth, row_bottom};
  rects.home = {rects.stop.right + kPadding, top, rects.stop.right + kPadding + kHomeButtonWidth, row_bottom};

  rects.privacy = {width - kPadding - kBadgeWidth, top, width - kPadding, row_bottom};
  rects.profile = {rects.privacy.left - kPadding - kBadgeWidth, top, rects.privacy.left - kPadding, row_bottom};

  const LONG address_left = rects.home.right + kPadding;
  const LONG address_right = std::max<LONG>(address_left + 160, rects.profile.left - kPadding);
  rects.address = {address_left, top, address_right, row_bottom};
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

  // Treat plain text as a search query so the omnibox behaves like a browser,
  // not like a strict URL field.
  return BuildSearchUrl(value, search_url_template);
}

}  // namespace velox::browser::win32
