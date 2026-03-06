#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "profiling/metrics_recorder.h"
#include "settings/app_settings.h"

namespace velox::app {

class SitePredictor {
 public:
  SitePredictor(const settings::AppSettings& settings, profiling::MetricsRecorder* metrics);
  ~SitePredictor();

  void Start();
  void RecordNavigation(const std::wstring& url_or_host);
  void Shutdown();

 private:
  struct HostEntry {
    int score = 0;
    std::uint64_t last_seen_epoch = 0;
  };

  void LoadState();
  void SaveState();
  std::vector<std::wstring> CollectWarmHosts() const;
  void WarmHosts(std::vector<std::wstring> hosts);

  settings::AppSettings settings_;
  profiling::MetricsRecorder* metrics_ = nullptr;
  std::filesystem::path state_path_;
  mutable std::mutex mutex_;
  std::unordered_map<std::wstring, HostEntry> host_entries_;
  std::thread warmup_thread_;
  std::atomic<bool> shutdown_requested_{false};
  bool started_ = false;
};

}  // namespace velox::app
