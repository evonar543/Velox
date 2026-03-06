#include <cassert>
#include <filesystem>

#include "platform/win/file_utils.h"
#include "profiling/metrics_recorder.h"

int wmain() {
  namespace fs = std::filesystem;
  const fs::path base_dir = velox::platform::GetExecutableDir() / L"metrics-test-data";
  velox::platform::EnsureDirectory(base_dir);

  velox::profiling::MetricsRecorder recorder;
  recorder.SetEnabled(true);
  recorder.SetOutputPath(base_dir / L"metrics.jsonl");
  recorder.Mark("startup.entry");
  recorder.RecordNumeric("first_paint", 12.5);
  recorder.RecordText("status", "ok");
  recorder.RecordMemory("idle");
  recorder.Flush();

  const auto events = recorder.Snapshot();
  assert(!events.empty());
  assert(fs::exists(base_dir / L"metrics.jsonl"));
  return 0;
}
