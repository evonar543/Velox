#pragma once

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace velox::profiling {

struct MetricEvent {
  std::string name;
  std::uint64_t timestamp_us = 0;
  std::optional<double> numeric_value;
  std::string text_value;
};

class MetricsRecorder {
 public:
  MetricsRecorder();

  void SetEnabled(bool enabled);
  void SetOutputPath(const std::filesystem::path& output_path);

  void Mark(std::string_view name);
  void RecordNumeric(std::string_view name, double value);
  void RecordText(std::string_view name, std::string_view value);
  void RecordMemory(std::string_view phase);
  void Flush();

  std::vector<MetricEvent> Snapshot() const;

 private:
  void AppendEvent(MetricEvent event);
  std::string SerializeEvent(const MetricEvent& event) const;
  static std::string Escape(std::string_view value);

  mutable std::mutex mutex_;
  std::vector<MetricEvent> events_;
  std::filesystem::path output_path_;
  bool enabled_ = true;
  std::uint64_t start_us_ = 0;
};

}  // namespace velox::profiling
