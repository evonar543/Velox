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

  if (settings.search.provider_name.empty()) {
    settings.search.provider_name = L"Google";
  }
  if (settings.search.query_url_template.empty()) {
    settings.search.query_url_template = L"https://www.google.com/search?q={query}";
  }
  if (settings.search.query_url_template.find(L"{query}") == std::wstring::npos) {
    settings.search.query_url_template += settings.search.query_url_template.find(L'?') == std::wstring::npos ? L"?q={query}" : L"&q={query}";
  }

  if (settings.optimization.predictor_host_count < 1) {
    settings.optimization.predictor_host_count = 1;
  }
  if (settings.optimization.predictor_host_count > 12) {
    settings.optimization.predictor_host_count = 12;
  }
  if (settings.optimization.max_cache_size_mb < 128) {
    settings.optimization.max_cache_size_mb = 128;
  }
  if (settings.optimization.cache_trim_target_percent < 55) {
    settings.optimization.cache_trim_target_percent = 55;
  }
  if (settings.optimization.cache_trim_target_percent > 95) {
    settings.optimization.cache_trim_target_percent = 95;
  }
}

}  // namespace velox::settings
