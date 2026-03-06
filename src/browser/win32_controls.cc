#include "browser/win32_controls.h"

#include <commctrl.h>

#include <algorithm>
#include <cwctype>

namespace velox::browser::win32 {

namespace {

constexpr int kPadding = 8;
constexpr int kButtonWidth = 84;

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
  const int address_left = kPadding + (kButtonWidth + kPadding) * 4;

  rects.back = {kPadding, kPadding, kPadding + kButtonWidth, kToolbarHeight - kPadding};
  rects.forward = {rects.back.right + kPadding, kPadding, rects.back.right + kPadding + kButtonWidth, kToolbarHeight - kPadding};
  rects.reload = {rects.forward.right + kPadding, kPadding, rects.forward.right + kPadding + kButtonWidth, kToolbarHeight - kPadding};
  rects.stop = {rects.reload.right + kPadding, kPadding, rects.reload.right + kPadding + kButtonWidth, kToolbarHeight - kPadding};
  rects.address = {address_left, kPadding, std::max(address_left + 120, width - kPadding), kToolbarHeight - kPadding};
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
