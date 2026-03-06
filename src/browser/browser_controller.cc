#include "browser/browser_controller.h"

namespace velox::browser {

void BrowserController::SetBrowser(CefRefPtr<CefBrowser> browser) {
  browser_ = browser;
}

void BrowserController::Reset() {
  browser_ = nullptr;
  is_loading_ = false;
  can_go_back_ = false;
  can_go_forward_ = false;
}

void BrowserController::Navigate(const std::wstring& url) const {
  if (browser_ == nullptr) {
    return;
  }
  browser_->GetMainFrame()->LoadURL(url);
}

void BrowserController::GoBack() const {
  if (browser_ != nullptr && browser_->CanGoBack()) {
    browser_->GoBack();
  }
}

void BrowserController::GoForward() const {
  if (browser_ != nullptr && browser_->CanGoForward()) {
    browser_->GoForward();
  }
}

void BrowserController::Reload() const {
  if (browser_ != nullptr) {
    browser_->Reload();
  }
}

void BrowserController::Stop() const {
  if (browser_ != nullptr) {
    browser_->StopLoad();
  }
}

void BrowserController::CloseBrowser() const {
  if (browser_ != nullptr) {
    browser_->GetHost()->CloseBrowser(false);
  }
}

void BrowserController::SetLoadingState(bool is_loading, bool can_go_back, bool can_go_forward) {
  is_loading_ = is_loading;
  can_go_back_ = can_go_back;
  can_go_forward_ = can_go_forward;
}

bool BrowserController::has_browser() const {
  return browser_ != nullptr;
}

bool BrowserController::is_loading() const {
  return is_loading_;
}

bool BrowserController::can_go_back() const {
  return can_go_back_;
}

bool BrowserController::can_go_forward() const {
  return can_go_forward_;
}

CefRefPtr<CefBrowser> BrowserController::browser() const {
  return browser_;
}

}  // namespace velox::browser
