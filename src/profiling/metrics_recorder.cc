#include "profiling/metrics_recorder.h"

#include <fstream>
#include <iomanip>
#include <sstream>

#include "platform/win/file_utils.h"
#include "platform/win/logger.h"
#include "profiling/memory_sampler.h"
#include "profiling/perf_timer.h"

namespace velox::profiling {

MetricsRecorder::MetricsRecorder() : start_us_(PerfTimer::NowMicroseconds()) {}

void MetricsRecorder::SetEnabled(bool enabled) {
  std::scoped_lock lock(mutex_);
  enabled_ = enabled;
}

void MetricsRecorder::SetOutputPath(const std::filesystem::path& output_path) {
  std::scoped_lock lock(mutex_);
  output_path_ = output_path;
  if (!output_path_.empty() && output_path_.has_parent_path()) {
    platform::EnsureDirectory(output_path_.parent_path());
  }
}

void MetricsRecorder::Mark(std::string_view name) {
  AppendEvent(MetricEvent{std::string(name), PerfTimer::NowMicroseconds() - start_us_, std::nullopt, {}});
}

void MetricsRecorder::RecordNumeric(std::string_view name, double value) {
  AppendEvent(MetricEvent{std::string(name), PerfTimer::NowMicroseconds() - start_us_, value, {}});
}

void MetricsRecorder::RecordText(std::string_view name, std::string_view value) {
  AppendEvent(MetricEvent{std::string(name), PerfTimer::NowMicroseconds() - start_us_, std::nullopt, std::string(value)});
}

void MetricsRecorder::RecordMemory(std::string_view phase) {
  const auto snapshot = SampleCurrentProcessMemory();
  if (!snapshot.has_value()) {
    return;
  }

  RecordNumeric(std::string(phase) + ".working_set_mb", static_cast<double>(snapshot->working_set_bytes) / (1024.0 * 1024.0));
  RecordNumeric(std::string(phase) + ".private_mb", static_cast<double>(snapshot->private_bytes) / (1024.0 * 1024.0));
}

void MetricsRecorder::Flush() {
  std::vector<MetricEvent> events;
  std::filesystem::path output_path;
  {
    std::scoped_lock lock(mutex_);
    if (!enabled_ || output_path_.empty()) {
      return;
    }
    events = events_;
    output_path = output_path_;
  }

  if (output_path.has_parent_path()) {
    platform::EnsureDirectory(output_path.parent_path());
  }

  std::ofstream stream(output_path, std::ios::out | std::ios::trunc);
  if (!stream.is_open()) {
    platform::LogWarning("Failed to open metrics output file.");
    return;
  }

  for (const auto& event : events) {
    stream << SerializeEvent(event) << "\n";
  }
}

std::vector<MetricEvent> MetricsRecorder::Snapshot() const {
  std::scoped_lock lock(mutex_);
  return events_;
}

void MetricsRecorder::AppendEvent(MetricEvent event) {
  std::scoped_lock lock(mutex_);
  if (!enabled_) {
    return;
  }
  events_.push_back(std::move(event));
}

std::string MetricsRecorder::SerializeEvent(const MetricEvent& event) const {
  std::ostringstream stream;
  stream << "{\"name\":\"" << Escape(event.name) << "\",\"timestamp_us\":" << event.timestamp_us;
  if (event.numeric_value.has_value()) {
    stream << ",\"value\":" << std::fixed << std::setprecision(3) << *event.numeric_value;
  }
  if (!event.text_value.empty()) {
    stream << ",\"text\":\"" << Escape(event.text_value) << "\"";
  }
  stream << "}";
  return stream.str();
}

std::string MetricsRecorder::Escape(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const char ch : value) {
    switch (ch) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        escaped.push_back(ch);
        break;
    }
  }
  return escaped;
}

}  // namespace velox::profiling
