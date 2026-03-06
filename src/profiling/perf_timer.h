#pragma once

#include <cstdint>

namespace velox::profiling {

class PerfTimer {
 public:
  static std::uint64_t NowMicroseconds();
};

}  // namespace velox::profiling
