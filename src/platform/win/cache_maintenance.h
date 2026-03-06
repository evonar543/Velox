#pragma once

#include <cstdint>
#include <filesystem>

namespace velox::platform {

struct CacheMaintenanceResult {
  std::uint64_t size_before_bytes = 0;
  std::uint64_t size_after_bytes = 0;
  std::uint64_t bytes_removed = 0;
  std::uint64_t removed_files = 0;
  bool trimmed = false;
};

CacheMaintenanceResult EnforceCacheBudget(const std::filesystem::path& cache_dir,
                                          std::uint64_t max_size_bytes,
                                          int trim_target_percent);

}  // namespace velox::platform
