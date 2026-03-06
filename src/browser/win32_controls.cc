#include "browser/win32_controls.h"

#include <commctrl.h>

#include <algorithm>
#include <cwctype>

namespace velox::browser::win32 {

namespace {

constexpr int kPadding = 12;
constexpr int kButtonWidth = 44;
constexpr int kControlHeight = 32;
constexpr int kBrandWidth = 96;
constexpr int kBadgeWidth = 120;
constexpr int kStatusHeight = 16;
constexpr int kProgressHeight = 4;

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
  const int status_top = row_bottom + 8;
  const int progress_top = status_top + kStatusHeight + 8;

  rects.brand = {kPadding, top, kPadding + kBrandWidth, row_bottom};
  rects.back = {rects.brand.right + kPadding, top, rects.brand.right + kPadding + kButtonWidth, row_bottom};
  rects.forward = {rects.back.right + kPadding, top, rects.back.right + kPadding + kButtonWidth, row_bottom};
  rects.reload = {rects.forward.right + kPadding, top, rects.forward.right + kPadding + kButtonWidth, row_bottom};
  rects.stop = {rects.reload.right + kPadding, top, rects.reload.right + kPadding + kButtonWidth, row_bottom};

  rects.privacy = {width - kPadding - kBadgeWidth, top, width - kPadding, row_bottom};
  rects.profile = {rects.privacy.left - kPadding - kBadgeWidth, top, rects.privacy.left - kPadding, row_bottom};

  const LONG address_left = rects.stop.right + kPadding;
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

std::wstring NormalizeAddressInput(std::wstring value) {
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](wchar_t ch) { return !iswspace(ch); }));
  value.erase(std::find_if(value.rbegin(), value.rend(), [](wchar_t ch) { return !iswspace(ch); }).base(), value.end());

  if (value.empty()) {
    return value;
  }
  if (value.find(L"://") == std::wstring::npos) {
    return L"https://" + value;
  }
  return value;
}

}  // namespace velox::browser::win32
