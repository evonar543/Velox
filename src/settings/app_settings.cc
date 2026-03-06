#include "settings/app_settings.h"

#include "platform/win/file_utils.h"

namespace velox::settings {

AppSettings DefaultAppSettings(const std::filesystem::path& base_dir) {
  AppSettings settings;
  settings.paths.profile_dir = base_dir / L"profile";
  settings.paths.cache_dir = base_dir / L"profile" / L"cache";
  settings.paths.log_dir = base_dir / L"logs";
  settings.paths.log_file = base_dir / L"logs" / L"velox.log";
  settings.benchmarking.output_file = base_dir / L"logs" / L"metrics.jsonl";
  return settings;
}

void FinalizeSettings(AppSettings& settings, const std::filesystem::path& base_dir) {
  if (settings.paths.cache_dir.empty() && !settings.paths.profile_dir.empty()) {
    settings.paths.cache_dir = settings.paths.profile_dir / L"cache";
  }
  if (settings.paths.log_file.empty() && !settings.paths.log_dir.empty()) {
    settings.paths.log_file = settings.paths.log_dir / L"velox.log";
  }
  if (settings.paths.log_dir.empty() && !settings.paths.log_file.empty()) {
    settings.paths.log_dir = settings.paths.log_file.parent_path();
  }
  if (settings.paths.log_dir.empty()) {
    settings.paths.log_dir = base_dir / L"logs";
  }
  if (settings.paths.log_file.empty()) {
    settings.paths.log_file = settings.paths.log_dir / L"velox.log";
  }

  settings.paths.profile_dir = platform::MakeAbsolute(base_dir, settings.paths.profile_dir);
  settings.paths.cache_dir = platform::MakeAbsolute(base_dir, settings.paths.cache_dir);
  settings.paths.log_dir = platform::MakeAbsolute(base_dir, settings.paths.log_dir);
  settings.paths.log_file = platform::MakeAbsolute(base_dir, settings.paths.log_file);
  settings.benchmarking.output_file = platform::MakeAbsolute(base_dir, settings.benchmarking.output_file);

  if (settings.window.width < 800) {
    settings.window.width = 800;
  }
  if (settings.window.height < 600) {
    settings.window.height = 600;
  }
}

}  // namespace velox::settings
