#include "app/app_bootstrap.h"

#include <filesystem>

#include "platform/win/crash_handler.h"
#include "platform/win/cache_maintenance.h"
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

  MaintainCacheBudget();

  runtime_profile_ = DetectRuntimeProfile(settings_.optimization);
  metrics_.RecordText("runtime.profile", RuntimeTierToString(runtime_profile_.tier));
  metrics_.RecordNumeric("runtime.logical_cores", static_cast<double>(runtime_profile_.logical_cores));
  metrics_.RecordNumeric("runtime.memory_mb", static_cast<double>(runtime_profile_.physical_memory_mb));
  metrics_.RecordNumeric("runtime.renderer_process_limit", static_cast<double>(runtime_profile_.renderer_process_limit));
  site_predictor_ = std::make_unique<SitePredictor>(settings_, &metrics_);
  site_predictor_->Start();

  if (!InitializeCef(main_args)) {
    return 1;
  }

  metrics_.Mark("cef.initialized");
  metrics_.RecordMemory("after_cef_init");

  browser_window_ =
      std::make_unique<browser::BrowserWindow>(instance_, settings_, command_line_, runtime_profile_, &metrics_, site_predictor_.get());
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

void AppBootstrap::MaintainCacheBudget() {
  if (settings_.incognito_default) {
    return;
  }

  const std::uint64_t max_cache_size_bytes =
      static_cast<std::uint64_t>(settings_.optimization.max_cache_size_mb) * 1024ULL * 1024ULL;
  const auto result =
      platform::EnforceCacheBudget(settings_.paths.cache_dir, max_cache_size_bytes, settings_.optimization.cache_trim_target_percent);

  metrics_.RecordNumeric("cache.size_before_mb", static_cast<double>(result.size_before_bytes) / (1024.0 * 1024.0));
  metrics_.RecordNumeric("cache.size_after_mb", static_cast<double>(result.size_after_bytes) / (1024.0 * 1024.0));

  if (result.trimmed) {
    metrics_.RecordNumeric("cache.trimmed_mb", static_cast<double>(result.bytes_removed) / (1024.0 * 1024.0));
    metrics_.RecordNumeric("cache.trimmed_files", static_cast<double>(result.removed_files));
    platform::LogInfo("Trimmed on-disk cache to stay within the configured budget.");
  }
}

bool AppBootstrap::InitializeCef(const CefMainArgs& main_args) {
  const std::filesystem::path executable_path = platform::GetExecutablePath();
  const std::filesystem::path base_dir = executable_path.parent_path();

  CefSettings cef_settings;
  // Let CEF own its UI thread so Chromium work stays responsive even if the
  // host window is busy processing native messages.
  cef_settings.no_sandbox = true;
  cef_settings.multi_threaded_message_loop = true;
  cef_settings.windowless_rendering_enabled = false;
  cef_settings.command_line_args_disabled = false;
  cef_settings.persist_session_cookies = false;
  cef_settings.log_severity = ToCefLogSeverity(settings_.log_level);
  cef_app_->SetRuntimeProfile(runtime_profile_);
  // Make the packaged app self-contained so it can be launched from any
  // working directory without CEF guessing where resources live.
  CefString(&cef_settings.browser_subprocess_path) = executable_path.wstring();
  CefString(&cef_settings.resources_dir_path) = base_dir.wstring();
  CefString(&cef_settings.locales_dir_path) = (base_dir / L"locales").wstring();
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

  browser_window_.reset();
  if (site_predictor_ != nullptr) {
    site_predictor_->Shutdown();
    site_predictor_.reset();
  }
  metrics_.Flush();
  if (cef_initialized_) {
    CefShutdown();
  }
  platform::Logger::Instance().Shutdown();
}

}  // namespace velox::app
