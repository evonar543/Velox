#pragma once

#include <optional>

#include "app/runtime_profile.h"
#include "include/cef_app.h"
#include "settings/app_settings.h"

namespace velox::cef {

class VeloxCefApp : public CefApp,
                    public CefBrowserProcessHandler,
                    public CefRenderProcessHandler {
 public:
  VeloxCefApp() = default;

  void SetRuntimeProfile(const app::RuntimeProfile& profile);
  void SetAppSettings(const settings::AppSettings& settings);

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
  std::optional<settings::AppSettings> app_settings_;

  IMPLEMENT_REFCOUNTING(VeloxCefApp);
};

}  // namespace velox::cef
