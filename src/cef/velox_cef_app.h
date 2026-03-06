#pragma once

#include <optional>

#include "app/runtime_profile.h"
#include "include/cef_app.h"

namespace velox::cef {

class VeloxCefApp : public CefApp,
                    public CefBrowserProcessHandler,
                    public CefRenderProcessHandler {
 public:
  VeloxCefApp() = default;

  void SetRuntimeProfile(const app::RuntimeProfile& profile);

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override;
  CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override;
  void OnBeforeCommandLineProcessing(const CefString& process_type,
                                     CefRefPtr<CefCommandLine> command_line) override;
  void OnBeforeChildProcessLaunch(CefRefPtr<CefCommandLine> command_line) override;
  void OnContextCreated(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        CefRefPtr<CefV8Context> context) override;

 private:
  std::optional<app::RuntimeProfile> runtime_profile_;

  IMPLEMENT_REFCOUNTING(VeloxCefApp);
};

}  // namespace velox::cef
