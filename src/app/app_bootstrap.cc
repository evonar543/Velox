#include "app/app_bootstrap.h"

#include <filesystem>

#include "platform/win/crash_handler.h"
#include "platform/win/file_utils.h"
#include "platform/win/logger.h"
#include "settings/settings_loader.h"

namespace velox::app {

namespace {

cef_log_severity_t ToCefLogSeverity(platform::LogLevel level) {
  switch (level) {
    case platform::LogLevel::kTrace:
      return LOGSEVERITY_VERBOSE;
    case platform::LogLevel::kInfo:
      return LOGSEVERITY_INFO;
    case platform::LogLevel::kWarning:
      return LOGSEVERITY_WARNING;
    case platform::LogLevel::kError:
      return LOGSEVERITY_ERROR;
  }
  return LOGSEVERITY_INFO;
}

}  // namespace

AppBootstrap::AppBootstrap(HINSTANCE instance, int command_show)
    : instance_(instance), command_show_(command_show) {}

int AppBootstrap::Run() {
  CefMainArgs main_args(instance_);
  cef_app_ = new cef::VeloxCefApp();

  const int subprocess_exit_code = CefExecuteProcess(main_args, cef_app_.get(), nullptr);
  if (subprocess_exit_code >= 0) {
    return subprocess_exit_code;
  }

  command_line_ = ParseCommandLine();

  if (!InitializeDiagnostics()) {
    return 1;
  }
  if (!InitializeSettings()) {
    return 1;
  }

  metrics_.SetEnabled(settings_.benchmarking.enabled);
  metrics_.SetOutputPath(settings_.benchmarking.output_file);
  metrics_.Mark("startup.entry");

  if (!InitializeCef(main_args)) {
    return 1;
  }

  metrics_.Mark("cef.initialized");
  metrics_.RecordMemory("after_cef_init");

  browser_window_ = std::make_unique<browser::BrowserWindow>(instance_, settings_, command_line_, &metrics_);
  if (!browser_window_->Create()) {
    platform::LogError("Failed to create the main window.");
    Shutdown();
    return 1;
  }

  browser_window_->Show(command_show_);
  metrics_.Mark("window.shown");

  if (!browser_window_->CreateBrowserShell()) {
    platform::LogError("Failed to create the CEF browser.");
    Shutdown();
    return 1;
  }

  return RunMessageLoop();
}

bool AppBootstrap::InitializeDiagnostics() {
  const std::filesystem::path base_dir = platform::GetExecutableDir();
  const std::filesystem::path default_log_path = base_dir / L"logs" / L"velox.log";
  const std::filesystem::path requested_log_path =
      command_line_.log_file.has_value() ? platform::MakeAbsolute(base_dir, *command_line_.log_file) : default_log_path;

  if (!platform::Logger::Instance().Initialize(requested_log_path, platform::LogLevel::kInfo)) {
    return false;
  }

  platform::CrashHandler::Install(base_dir / L"logs" / L"dumps");
  platform::LogInfo("Bootstrap diagnostics initialized.");
  return true;
}

bool AppBootstrap::InitializeSettings() {
  const std::filesystem::path base_dir = platform::GetExecutableDir();
  const std::filesystem::path config_path = base_dir / L"config" / L"settings.json";
  settings_ = settings::LoadSettings(config_path, base_dir, command_line_);

  if (!platform::Logger::Instance().Initialize(settings_.paths.log_file, settings_.log_level)) {
    return false;
  }

  platform::CrashHandler::Install(settings_.paths.log_dir / L"dumps");
  platform::LogInfo("Settings loaded from " + platform::ToUtf8(config_path.wstring()));
  return true;
}

bool AppBootstrap::InitializeCef(const CefMainArgs& main_args) {
  CefSettings cef_settings;
  // Let CEF own its UI thread so Chromium work stays responsive even if the
  // host window is busy processing native messages.
  cef_settings.no_sandbox = true;
  cef_settings.multi_threaded_message_loop = true;
  cef_settings.windowless_rendering_enabled = false;
  cef_settings.command_line_args_disabled = false;
  cef_settings.persist_session_cookies = false;
  cef_settings.log_severity = ToCefLogSeverity(settings_.log_level);
  platform::EnsureDirectory(settings_.paths.profile_dir);
  platform::EnsureDirectory(settings_.paths.cache_dir);
  CefString(&cef_settings.root_cache_path) = settings_.paths.profile_dir.wstring();
  CefString(&cef_settings.log_file) = (settings_.paths.log_dir / L"cef.log").wstring();

  const bool initialized = CefInitialize(main_args, cef_settings, cef_app_.get(), nullptr);
  if (!initialized) {
    platform::LogError("CefInitialize failed.");
    return false;
  }
  cef_initialized_ = true;
  return true;
}

int AppBootstrap::RunMessageLoop() {
  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }

  metrics_.RecordMemory("shutdown");
  Shutdown();
  return static_cast<int>(message.wParam);
}

void AppBootstrap::Shutdown() {
  if (shut_down_) {
    return;
  }
  shut_down_ = true;

  metrics_.Flush();
  browser_window_.reset();
  if (cef_initialized_) {
    CefShutdown();
  }
  platform::Logger::Instance().Shutdown();
}

}  // namespace velox::app
