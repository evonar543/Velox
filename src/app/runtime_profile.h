#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "settings/app_settings.h"

namespace velox::app {

enum class RuntimeTier {
  kLean,
  kBalanced,
  kTurbo,
};

struct RuntimeProfile {
  RuntimeTier tier = RuntimeTier::kBalanced;
  int logical_cores = 1;
  std::uint64_t physical_memory_mb = 0;
  int renderer_process_limit = 4;
  bool prefer_low_memory_mode = false;
};

RuntimeProfile DetectRuntimeProfile(const settings::OptimizationSettings& optimization);
std::string RuntimeTierToString(RuntimeTier tier);
std::wstring RuntimeTierToLabel(RuntimeTier tier);
std::wstring DescribeRuntimeProfile(const RuntimeProfile& profile);

}  // namespace velox::app
