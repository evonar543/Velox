#include "profiling/memory_sampler.h"

#include <Windows.h>
#include <psapi.h>

namespace velox::profiling {

std::optional<MemorySnapshot> SampleCurrentProcessMemory() {
  PROCESS_MEMORY_COUNTERS_EX counters{};
  counters.cb = sizeof(counters);

  if (!GetProcessMemoryInfo(GetCurrentProcess(),
                            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                            sizeof(counters))) {
    return std::nullopt;
  }

  MemorySnapshot snapshot;
  snapshot.working_set_bytes = counters.WorkingSetSize;
  snapshot.private_bytes = counters.PrivateUsage;
  return snapshot;
}

}  // namespace velox::profiling
