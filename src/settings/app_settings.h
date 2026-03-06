#pragma once

#include <filesystem>
#include <string>

#include "platform/win/logger.h"

namespace velox::settings {

struct WindowSettings {
  int width = 1280;
  int height = 800;
};

struct PathSettings {
  std::filesystem::path profile_dir;
  std::filesystem::path cache_dir;
  std::filesystem::path log_dir;
  std::filesystem::path log_file;
};

struct BenchmarkingSettings {
  bool enabled = true;
  std::filesystem::path output_file;
};

struct PrivacySettings {
  bool do_not_track = true;
  bool global_privacy_control = true;
  bool block_third_party_cookies = true;
  bool strip_tracking_query_parameters = true;
  bool strip_cross_site_referrers = true;
  bool block_webrtc_non_proxied_udp = true;
  bool disable_password_manager = true;
  bool block_external_protocols = true;
};

struct BlockingSettings {
  bool enabled = true;
  bool block_ads = true;
  bool block_trackers = true;
};

struct SearchSettings {
  std::wstring provider_name = L"Google";
  std::wstring query_url_template = L"https://www.google.com/search?q={query}";
};

struct OptimizationSettings {
  bool auto_tune = true;
  int renderer_process_limit = 0;
  bool predictive_warmup = true;
  int predictor_host_count = 4;
  int max_cache_size_mb = 512;
  int cache_trim_target_percent = 80;
};

struct AppSettings {
  std::wstring startup_url = L"https://example.com";
  WindowSettings window;
  PathSettings paths;
  BenchmarkingSettings benchmarking;
  PrivacySettings privacy;
  BlockingSettings blocking;
  SearchSettings search;
  OptimizationSettings optimization;
  platform::LogLevel log_level = platform::LogLevel::kInfo;
  bool incognito_default = false;
};

AppSettings DefaultAppSettings(const std::filesystem::path& base_dir);
void FinalizeSettings(AppSettings& settings, const std::filesystem::path& base_dir);

}  // namespace velox::settings
