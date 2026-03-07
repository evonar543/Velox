#include "cef/velox_cef_app.h"

#include <optional>
#include <string>

#include "cef/extension_support.h"
#include "cef/render_metrics_bridge.h"

namespace velox::cef {

namespace {

constexpr char kVeloxTierSwitch[] = "velox-runtime-tier";
constexpr char kVeloxRendererLimitSwitch[] = "velox-renderer-process-limit";
constexpr char kVeloxLowMemorySwitch[] = "velox-low-memory-mode";

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

std::optional<int> ReadIntSwitch(CefRefPtr<CefCommandLine> command_line, const char* name) {
  if (!command_line->HasSwitch(name)) {
    return std::nullopt;
  }

  try {
    const int value = std::stoi(command_line->GetSwitchValue(name).ToString());
    if (value > 0) {
      return value;
    }
  } catch (...) {
  }
  return std::nullopt;
}

std::optional<app::RuntimeTier> ReadRuntimeTier(CefRefPtr<CefCommandLine> command_line) {
  if (!command_line->HasSwitch(kVeloxTierSwitch)) {
    return std::nullopt;
  }

  const std::string tier = command_line->GetSwitchValue(kVeloxTierSwitch).ToString();
  if (tier == "lean") {
    return app::RuntimeTier::kLean;
  }
  if (tier == "turbo") {
    return app::RuntimeTier::kTurbo;
  }
  return app::RuntimeTier::kBalanced;
}

app::RuntimeProfile ResolveRuntimeProfile(CefRefPtr<CefCommandLine> command_line,
                                          const std::optional<app::RuntimeProfile>& configured_profile) {
  if (configured_profile.has_value()) {
    return *configured_profile;
  }

  // Subprocesses reconstruct the browser-chosen profile from forwarded switches
  // so every Chromium role stays in the same tuning envelope.
  app::RuntimeProfile profile;
  if (const auto tier = ReadRuntimeTier(command_line); tier.has_value()) {
    profile.tier = *tier;
  }
  if (const auto renderer_limit = ReadIntSwitch(command_line, kVeloxRendererLimitSwitch); renderer_limit.has_value()) {
    profile.renderer_process_limit = *renderer_limit;
  }
  profile.prefer_low_memory_mode = command_line->HasSwitch(kVeloxLowMemorySwitch);
  return profile;
}

}  // namespace

void VeloxCefApp::SetRuntimeProfile(const app::RuntimeProfile& profile) {
  runtime_profile_ = profile;
}

void VeloxCefApp::SetAppSettings(const settings::AppSettings& settings) {
  app_settings_ = settings;
}

CefRefPtr<CefBrowserProcessHandler> VeloxCefApp::GetBrowserProcessHandler() {
  return this;
}

CefRefPtr<CefRenderProcessHandler> VeloxCefApp::GetRenderProcessHandler() {
  return this;
}

void VeloxCefApp::OnBeforeCommandLineProcessing(const CefString& process_type,
                                                CefRefPtr<CefCommandLine> command_line) {
  (void)process_type;

  const app::RuntimeProfile runtime_profile = ResolveRuntimeProfile(command_line, runtime_profile_);

  // These switches trade Chrome-style background services for a leaner shell
  // with lower idle overhead and less network chatter.
  AppendSwitchIfMissing(command_line, "disable-background-networking");
  AppendSwitchIfMissing(command_line, "disable-component-update");
  AppendSwitchIfMissing(command_line, "disable-client-side-phishing-detection");
  AppendSwitchIfMissing(command_line, "disable-default-apps");
  AppendSwitchIfMissing(command_line, "disable-domain-reliability");
  AppendSwitchIfMissing(command_line, "disable-print-preview");
  AppendSwitchIfMissing(command_line, "disable-renderer-backgrounding");
  AppendSwitchIfMissing(command_line, "disable-sync");
  AppendSwitchIfMissing(command_line, "enable-gpu-rasterization");
  AppendSwitchIfMissing(command_line, "enable-zero-copy");
  AppendSwitchWithValueIfMissing(command_line, "renderer-process-limit",
                                 std::to_string(runtime_profile.renderer_process_limit));
  AppendDisableFeatures(command_line,
                        "AutofillServerCommunication,CertificateTransparencyComponentUpdater,"
                        "CalculateNativeWinOcclusion,MediaRouter,OptimizationHints,Translate");

  if (runtime_profile.prefer_low_memory_mode) {
    AppendDisableFeatures(command_line, "BackForwardCache");
  }

  if (process_type.empty() && app_settings_.has_value()) {
    if (ExtensionsEnabled(*app_settings_)) {
      ApplyExtensionSwitches(command_line, *app_settings_);
    } else {
      AppendSwitchIfMissing(command_line, "disable-extensions");
    }
  }
}

void VeloxCefApp::OnBeforeChildProcessLaunch(CefRefPtr<CefCommandLine> command_line) {
  const app::RuntimeProfile runtime_profile = runtime_profile_.value_or(app::RuntimeProfile{});
  // Push our derived profile into child processes explicitly; relying on each
  // subprocess to auto-detect the machine would make experimentation noisier.
  AppendSwitchWithValueIfMissing(command_line, kVeloxTierSwitch, app::RuntimeTierToString(runtime_profile.tier));
  AppendSwitchWithValueIfMissing(command_line, kVeloxRendererLimitSwitch,
                                 std::to_string(runtime_profile.renderer_process_limit));
  if (runtime_profile.prefer_low_memory_mode) {
    AppendSwitchIfMissing(command_line, kVeloxLowMemorySwitch);
  }
}

void VeloxCefApp::OnContextCreated(CefRefPtr<CefBrowser> browser,
                                   CefRefPtr<CefFrame> frame,
                                   CefRefPtr<CefV8Context> context) {
  (void)browser;
  InstallRenderMetricsBridge(frame, context);
}

}  // namespace velox::cef
