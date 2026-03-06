#pragma once

#include <cstdint>
#include <optional>

namespace velox::profiling {

struct MemorySnapshot {
  std::uint64_t working_set_bytes = 0;
  std::uint64_t private_bytes = 0;
};

std::optional<MemorySnapshot> SampleCurrentProcessMemory();

}  // namespace velox::profiling
