#pragma once

#include "include/cef_browser.h"

namespace velox::browser {

class BrowserController {
 public:
  void SetBrowser(CefRefPtr<CefBrowser> browser);
  void Reset();

  void Navigate(const std::wstring& url) const;
  void GoBack() const;
  void GoForward() const;
  void Reload() const;
  void Stop() const;
  void CloseBrowser() const;

  void SetLoadingState(bool is_loading, bool can_go_back, bool can_go_forward);

  bool has_browser() const;
  bool is_loading() const;
  bool can_go_back() const;
  bool can_go_forward() const;
  CefRefPtr<CefBrowser> browser() const;

 private:
  CefRefPtr<CefBrowser> browser_;
  bool is_loading_ = false;
  bool can_go_back_ = false;
  bool can_go_forward_ = false;
};

}  // namespace velox::browser
