#pragma once

#include <Windows.h>

#include <string>

namespace velox::browser::win32 {

constexpr int kBackButtonId = 1001;
constexpr int kForwardButtonId = 1002;
constexpr int kReloadButtonId = 1003;
constexpr int kStopButtonId = 1004;
constexpr int kAddressBarId = 1005;
constexpr int kBrandLabelId = 1006;
constexpr int kProfileBadgeId = 1007;
constexpr int kPrivacyBadgeId = 1008;
constexpr int kStatusLabelId = 1009;
constexpr int kProgressBarId = 1010;
constexpr WORD kAddressEnterNotification = 0x7010;
constexpr int kToolbarHeight = 92;

struct LayoutRects {
  RECT brand{};
  RECT back{};
  RECT forward{};
  RECT reload{};
  RECT stop{};
  RECT address{};
  RECT profile{};
  RECT privacy{};
  RECT status{};
  RECT progress{};
  RECT browser{};
};

LayoutRects ComputeLayout(const RECT& client_rect);
void InstallAddressBarSubclass(HWND address_bar, HWND parent);
std::wstring NormalizeAddressInput(std::wstring value);

}  // namespace velox::browser::win32
