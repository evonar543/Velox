#include "app/runtime_profile.h"

#include <Windows.h>

#include <algorithm>
#include <sstream>

namespace velox::app {

namespace {

constexpr std::uint64_t kMegabyte = 1024ull * 1024ull;

}  // namespace

RuntimeProfile DetectRuntimeProfile(const settings::OptimizationSettings& optimization) {
  SYSTEM_INFO system_info{};
  GetNativeSystemInfo(&system_info);

  MEMORYSTATUSEX memory_status{};
  memory_status.dwLength = sizeof(memory_status);
  GlobalMemoryStatusEx(&memory_status);

  RuntimeProfile profile;
  profile.logical_cores = static_cast<int>(std::max<DWORD>(1, system_info.dwNumberOfProcessors));
  profile.physical_memory_mb = memory_status.ullTotalPhys / kMegabyte;
  profile.prefer_low_memory_mode = profile.physical_memory_mb > 0 && profile.physical_memory_mb <= 12288;

  if (optimization.renderer_process_limit > 0) {
    profile.renderer_process_limit = optimization.renderer_process_limit;
  } else if (!optimization.auto_tune) {
    profile.renderer_process_limit = 4;
  } else if (profile.physical_memory_mb <= 8192 || profile.logical_cores <= 4) {
    profile.tier = RuntimeTier::kLean;
    profile.renderer_process_limit = 2;
  } else if (profile.physical_memory_mb <= 16384 || profile.logical_cores <= 8) {
    profile.tier = RuntimeTier::kBalanced;
    profile.renderer_process_limit = 4;
  } else {
    profile.tier = RuntimeTier::kTurbo;
    profile.renderer_process_limit = 6;
  }

  if (optimization.renderer_process_limit > 0) {
    if (profile.renderer_process_limit <= 2) {
      profile.tier = RuntimeTier::kLean;
    } else if (profile.renderer_process_limit <= 4) {
      profile.tier = RuntimeTier::kBalanced;
    } else {
      profile.tier = RuntimeTier::kTurbo;
    }
  }

  return profile;
}

std::string RuntimeTierToString(RuntimeTier tier) {
  switch (tier) {
    case RuntimeTier::kLean:
      return "lean";
    case RuntimeTier::kBalanced:
      return "balanced";
    case RuntimeTier::kTurbo:
      return "turbo";
  }
  return "balanced";
}

std::wstring RuntimeTierToLabel(RuntimeTier tier) {
  switch (tier) {
    case RuntimeTier::kLean:
      return L"Lean";
    case RuntimeTier::kBalanced:
      return L"Balanced";
    case RuntimeTier::kTurbo:
      return L"Turbo";
  }
  return L"Balanced";
}

std::wstring DescribeRuntimeProfile(const RuntimeProfile& profile) {
  std::wostringstream stream;
  stream << RuntimeTierToLabel(profile.tier) << L" auto-tune";
  if (profile.logical_cores > 0) {
    stream << L" | " << profile.logical_cores << L" cores";
  }
  if (profile.physical_memory_mb > 0) {
    stream << L" | " << profile.physical_memory_mb / 1024 << L" GB RAM";
  }
  return stream.str();
}

}  // namespace velox::app
