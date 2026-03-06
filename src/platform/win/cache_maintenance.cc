#include "platform/win/cache_maintenance.h"

#include <algorithm>
#include <system_error>
#include <vector>

namespace velox::platform {

namespace {

struct CacheFileEntry {
  std::filesystem::path path;
  std::uint64_t size_bytes = 0;
  std::filesystem::file_time_type last_write_time{};
};

}  // namespace

CacheMaintenanceResult EnforceCacheBudget(const std::filesystem::path& cache_dir,
                                          std::uint64_t max_size_bytes,
                                          int trim_target_percent) {
  CacheMaintenanceResult result;
  if (cache_dir.empty() || max_size_bytes == 0) {
    return result;
  }

  std::error_code error_code;
  if (!std::filesystem::exists(cache_dir, error_code) || !std::filesystem::is_directory(cache_dir, error_code)) {
    return result;
  }

  std::vector<CacheFileEntry> files;
  for (std::filesystem::recursive_directory_iterator iterator(cache_dir, error_code), end;
       iterator != end && !error_code;
       iterator.increment(error_code)) {
    if (!iterator->is_regular_file(error_code)) {
      continue;
    }

    const auto file_size = iterator->file_size(error_code);
    if (error_code) {
      error_code.clear();
      continue;
    }

    CacheFileEntry entry;
    entry.path = iterator->path();
    entry.size_bytes = file_size;
    entry.last_write_time = iterator->last_write_time(error_code);
    if (error_code) {
      error_code.clear();
      entry.last_write_time = std::filesystem::file_time_type::min();
    }

    result.size_before_bytes += entry.size_bytes;
    files.push_back(std::move(entry));
  }

  result.size_after_bytes = result.size_before_bytes;
  if (result.size_before_bytes <= max_size_bytes || files.empty()) {
    return result;
  }

  const int bounded_target = std::clamp(trim_target_percent, 55, 95);
  const std::uint64_t target_size = std::max<std::uint64_t>(1, (max_size_bytes * bounded_target) / 100);
  std::sort(files.begin(), files.end(), [](const CacheFileEntry& left, const CacheFileEntry& right) {
    return left.last_write_time < right.last_write_time;
  });

  for (const auto& entry : files) {
    if (result.size_after_bytes <= target_size) {
      break;
    }

    std::filesystem::remove(entry.path, error_code);
    if (error_code) {
      error_code.clear();
      continue;
    }

    result.size_after_bytes -= std::min(result.size_after_bytes, entry.size_bytes);
    result.bytes_removed += entry.size_bytes;
    ++result.removed_files;
    result.trimmed = true;
  }

  return result;
}

}  // namespace velox::platform
