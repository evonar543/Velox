#pragma once

#include "include/cef_request_context_handler.h"
#include "include/cef_resource_request_handler.h"
#include "profiling/metrics_recorder.h"
#include "settings/app_settings.h"

namespace velox::cef {

class BrowserPolicy : public CefRequestContextHandler,
                      public CefResourceRequestHandler,
                      public CefCookieAccessFilter {
 public:
  BrowserPolicy(settings::AppSettings settings, profiling::MetricsRecorder* metrics);

  void OnRequestContextInitialized(CefRefPtr<CefRequestContext> request_context) override;
  CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(
      CefRefPtr<CefBrowser> browser,
      CefRefPtr<CefFrame> frame,
      CefRefPtr<CefRequest> request,
      bool is_navigation,
      bool is_download,
      const CefString& request_initiator,
      bool& disable_default_handling) override;

  CefRefPtr<CefCookieAccessFilter> GetCookieAccessFilter(
      CefRefPtr<CefBrowser> browser,
      CefRefPtr<CefFrame> frame,
      CefRefPtr<CefRequest> request) override;

  ReturnValue OnBeforeResourceLoad(CefRefPtr<CefBrowser> browser,
                                   CefRefPtr<CefFrame> frame,
                                   CefRefPtr<CefRequest> request,
                                   CefRefPtr<CefCallback> callback) override;

  void OnProtocolExecution(CefRefPtr<CefBrowser> browser,
                           CefRefPtr<CefFrame> frame,
                           CefRefPtr<CefRequest> request,
                           bool& allow_os_execution) override;

  bool CanSendCookie(CefRefPtr<CefBrowser> browser,
                     CefRefPtr<CefFrame> frame,
                     CefRefPtr<CefRequest> request,
                     const CefCookie& cookie) override;

  bool CanSaveCookie(CefRefPtr<CefBrowser> browser,
                     CefRefPtr<CefFrame> frame,
                     CefRefPtr<CefRequest> request,
                     CefRefPtr<CefResponse> response,
                     const CefCookie& cookie) override;

 private:
  bool ShouldBlockRequest(CefRefPtr<CefRequest> request) const;
  void ApplyPrivacyHeaders(CefRefPtr<CefRequest> request) const;
  void ApplyContextPreferences(CefRefPtr<CefRequestContext> request_context) const;
  bool IsThirdPartyRequest(CefRefPtr<CefRequest> request) const;

  settings::AppSettings settings_;
  profiling::MetricsRecorder* metrics_ = nullptr;

  IMPLEMENT_REFCOUNTING(BrowserPolicy);
};

}  // namespace velox::cef
