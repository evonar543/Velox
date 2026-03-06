#pragma once

#include <filesystem>
#include <string>

namespace velox::platform {

std::filesystem::path GetExecutablePath();
std::filesystem::path GetExecutableDir();
bool EnsureDirectory(const std::filesystem::path& path);
std::filesystem::path MakeAbsolute(const std::filesystem::path& base, const std::filesystem::path& candidate);

std::wstring ToWide(const std::string& value);
std::string ToUtf8(const std::wstring& value);

}  // namespace velox::platform
