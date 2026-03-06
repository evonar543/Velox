#pragma once

#include "include/cef_app.h"

namespace velox::cef {

class VeloxCefApp : public CefApp,
                    public CefBrowserProcessHandler,
                    public CefRenderProcessHandler {
 public:
  VeloxCefApp() = default;

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override;
  CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override;
  void OnBeforeCommandLineProcessing(const CefString& process_type,
                                     CefRefPtr<CefCommandLine> command_line) override;
  void OnContextCreated(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        CefRefPtr<CefV8Context> context) override;

 private:
  IMPLEMENT_REFCOUNTING(VeloxCefApp);
};

}  // namespace velox::cef
