#include "cef/velox_cef_app.h"

#include <string>

#include "cef/render_metrics_bridge.h"

namespace velox::cef {

namespace {

void AppendSwitchIfMissing(CefRefPtr<CefCommandLine> command_line, const char* name) {
  if (!command_line->HasSwitch(name)) {
    command_line->AppendSwitch(name);
  }
}

void AppendSwitchWithValueIfMissing(CefRefPtr<CefCommandLine> command_line,
                                    const char* name,
                                    const std::string& value) {
  if (!command_line->HasSwitch(name)) {
    command_line->AppendSwitchWithValue(name, value);
  }
}

void AppendDisableFeatures(CefRefPtr<CefCommandLine> command_line, std::string_view features) {
  const std::string existing = command_line->HasSwitch("disable-features")
                                   ? command_line->GetSwitchValue("disable-features").ToString()
                                   : std::string{};
  if (existing.empty()) {
    command_line->AppendSwitchWithValue("disable-features", std::string(features));
    return;
  }
  if (existing.find(std::string(features)) != std::string::npos) {
    return;
  }
  command_line->AppendSwitchWithValue("disable-features", existing + "," + std::string(features));
}

}  // namespace

CefRefPtr<CefBrowserProcessHandler> VeloxCefApp::GetBrowserProcessHandler() {
  return this;
}

CefRefPtr<CefRenderProcessHandler> VeloxCefApp::GetRenderProcessHandler() {
  return this;
}

void VeloxCefApp::OnBeforeCommandLineProcessing(const CefString& process_type,
                                                CefRefPtr<CefCommandLine> command_line) {
  (void)process_type;

  // These switches trade Chrome-style background services for a leaner shell
  // with lower idle overhead and less network chatter.
  AppendSwitchIfMissing(command_line, "disable-background-networking");
  AppendSwitchIfMissing(command_line, "disable-component-update");
  AppendSwitchIfMissing(command_line, "disable-client-side-phishing-detection");
  AppendSwitchIfMissing(command_line, "disable-default-apps");
  AppendSwitchIfMissing(command_line, "disable-domain-reliability");
  AppendSwitchIfMissing(command_line, "disable-extensions");
  AppendSwitchIfMissing(command_line, "disable-print-preview");
  AppendSwitchIfMissing(command_line, "disable-sync");
  AppendSwitchIfMissing(command_line, "enable-gpu-rasterization");
  AppendSwitchIfMissing(command_line, "enable-zero-copy");
  AppendSwitchWithValueIfMissing(command_line, "renderer-process-limit", "4");
  AppendDisableFeatures(command_line,
                        "AutofillServerCommunication,CertificateTransparencyComponentUpdater,"
                        "CalculateNativeWinOcclusion,MediaRouter,OptimizationHints,Translate");
}

void VeloxCefApp::OnContextCreated(CefRefPtr<CefBrowser> browser,
                                   CefRefPtr<CefFrame> frame,
                                   CefRefPtr<CefV8Context> context) {
  (void)browser;
  InstallRenderMetricsBridge(frame, context);
}

}  // namespace velox::cef
