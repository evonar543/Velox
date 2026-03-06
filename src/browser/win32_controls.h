#pragma once

#include <Windows.h>

#include <string>

namespace velox::browser::win32 {

constexpr int kBackButtonId = 1001;
constexpr int kForwardButtonId = 1002;
constexpr int kReloadButtonId = 1003;
constexpr int kStopButtonId = 1004;
constexpr int kAddressBarId = 1005;
constexpr WORD kAddressEnterNotification = 0x7010;
constexpr int kToolbarHeight = 44;

struct LayoutRects {
  RECT back{};
  RECT forward{};
  RECT reload{};
  RECT stop{};
  RECT address{};
  RECT browser{};
};

LayoutRects ComputeLayout(const RECT& client_rect);
void InstallAddressBarSubclass(HWND address_bar, HWND parent);
std::wstring NormalizeAddressInput(std::wstring value);

}  // namespace velox::browser::win32
