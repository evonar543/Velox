#include <cassert>
#include <filesystem>
#include <fstream>

#include "app/command_line.h"
#include "platform/win/file_utils.h"
#include "settings/settings_loader.h"

int wmain() {
  namespace fs = std::filesystem;
  const fs::path base_dir = velox::platform::GetExecutableDir() / L"settings-test-data";
  const fs::path working_dir = fs::current_path();
  velox::platform::EnsureDirectory(base_dir);

  const fs::path config_path = base_dir / L"settings.json";
  std::ofstream config(config_path);
  config << "{\n"
            "  \"startup_url\": \"https://velox.test\",\n"
            "  \"window\": { \"width\": 1440, \"height\": 900 },\n"
            "  \"paths\": { \"profile_dir\": \"profile-a\", \"cache_dir\": \"profile-a/cache\", \"log_dir\": \"logs-a\" },\n"
            "  \"logging\": { \"level\": \"warning\" },\n"
            "  \"benchmarking\": { \"enabled\": true, \"output\": \"logs-a/bench.jsonl\" },\n"
            "  \"privacy\": {\n"
            "    \"do_not_track\": true,\n"
            "    \"global_privacy_control\": true,\n"
            "    \"block_third_party_cookies\": true,\n"
            "    \"strip_tracking_query_parameters\": true,\n"
            "    \"strip_cross_site_referrers\": true,\n"
            "    \"block_webrtc_non_proxied_udp\": true,\n"
            "    \"disable_password_manager\": true,\n"
            "    \"block_external_protocols\": true\n"
            "  },\n"
            "  \"blocking\": { \"enabled\": true, \"block_ads\": true, \"block_trackers\": true },\n"
            "  \"search\": {\n"
            "    \"provider_name\": \"Google\",\n"
            "    \"query_url_template\": \"https://www.google.com/search?q={query}\"\n"
            "  },\n"
            "  \"ui\": {\n"
            "    \"barebones_prototype\": true\n"
            "  },\n"
            "  \"extensions\": {\n"
            "    \"enabled\": true,\n"
            "    \"chrome_runtime\": true,\n"
            "    \"open_extensions_page_on_startup\": true,\n"
            "    \"allow_file_access\": true,\n"
            "    \"unpacked_dirs\": [\"extensions/a\", \"extensions/b\"],\n"
            "    \"extra_chromium_switches\": [\"extensions-on-chrome-urls\", \"show-component-extension-options\"]\n"
            "  },\n"
            "  \"optimization\": {\n"
            "    \"auto_tune\": true,\n"
            "    \"renderer_process_limit\": 0,\n"
            "    \"predictive_warmup\": true,\n"
            "    \"predictor_host_count\": 5,\n"
            "    \"max_cache_size_mb\": 768,\n"
            "    \"cache_trim_target_percent\": 75\n"
            "  },\n"
            "  \"incognito_default\": false\n"
            "}\n";
  config.close();

  velox::app::CommandLineOptions options;
  options.incognito = true;
  options.startup_url = L"https://override.test";
  options.profile_dir = L"profile-b";
  options.log_file = L"logs-b/custom.log";
  options.benchmark_output = L"logs-b/metrics.jsonl";
  options.extension_dirs.push_back(L"profile-b/custom-extensions");

  const auto settings = velox::settings::LoadSettings(config_path, base_dir, options);
  assert(settings.startup_url == L"https://override.test");
  assert(settings.window.width == 1440);
  assert(settings.window.height == 900);
  assert(settings.incognito_default);
  assert(settings.paths.profile_dir == working_dir / L"profile-b");
  assert(settings.paths.cache_dir.filename() == L"cache");
  assert(settings.paths.log_file == working_dir / L"logs-b" / L"custom.log");
  assert(settings.benchmarking.output_file == working_dir / L"logs-b" / L"metrics.jsonl");
  assert(settings.privacy.do_not_track);
  assert(settings.privacy.global_privacy_control);
  assert(settings.privacy.block_third_party_cookies);
  assert(settings.privacy.strip_tracking_query_parameters);
  assert(settings.privacy.strip_cross_site_referrers);
  assert(settings.privacy.block_webrtc_non_proxied_udp);
  assert(settings.privacy.disable_password_manager);
  assert(settings.privacy.block_external_protocols);
  assert(settings.blocking.enabled);
  assert(settings.blocking.block_ads);
  assert(settings.blocking.block_trackers);
  assert(settings.search.provider_name == L"Google");
  assert(settings.search.query_url_template == L"https://www.google.com/search?q={query}");
  assert(settings.ui.barebones_prototype);
  assert(settings.extensions.enabled);
  assert(settings.extensions.chrome_runtime);
  assert(settings.extensions.open_extensions_page_on_startup);
  assert(settings.extensions.allow_file_access);
  assert(settings.extensions.unpacked_dirs.size() == 3);
  assert(settings.extensions.unpacked_dirs[0] == base_dir / L"extensions/a");
  assert(settings.extensions.unpacked_dirs[1] == base_dir / L"extensions/b");
  assert(settings.extensions.unpacked_dirs[2] == working_dir / L"profile-b" / L"custom-extensions");
  assert(settings.extensions.extra_chromium_switches.size() == 2);
  assert(settings.extensions.extra_chromium_switches[0] == L"extensions-on-chrome-urls");
  assert(settings.extensions.extra_chromium_switches[1] == L"show-component-extension-options");
  assert(settings.optimization.auto_tune);
  assert(settings.optimization.renderer_process_limit == 0);
  assert(settings.optimization.predictive_warmup);
  assert(settings.optimization.predictor_host_count == 5);
  assert(settings.optimization.max_cache_size_mb == 768);
  assert(settings.optimization.cache_trim_target_percent == 75);

  auto saved_settings = settings;
  saved_settings.search.provider_name = L"DuckDuckGo";
  saved_settings.search.query_url_template = L"https://duckduckgo.com/?q={query}";
  assert(velox::settings::SaveProfilePreferences(saved_settings));

  auto reloaded_settings = settings;
  velox::settings::LoadProfilePreferences(&reloaded_settings);
  assert(reloaded_settings.search.provider_name == L"DuckDuckGo");
  assert(reloaded_settings.search.query_url_template == L"https://duckduckgo.com/?q={query}");
  return 0;
}
