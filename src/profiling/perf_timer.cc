#include "profiling/perf_timer.h"

#include <Windows.h>

namespace velox::profiling {

std::uint64_t PerfTimer::NowMicroseconds() {
  static LARGE_INTEGER frequency = [] {
    LARGE_INTEGER value{};
    QueryPerformanceFrequency(&value);
    return value;
  }();

  LARGE_INTEGER now{};
  QueryPerformanceCounter(&now);
  return static_cast<std::uint64_t>((now.QuadPart * 1000000ULL) / frequency.QuadPart);
}

}  // namespace velox::profiling
