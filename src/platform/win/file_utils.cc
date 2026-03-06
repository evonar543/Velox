#include "platform/win/file_utils.h"

#include <Windows.h>

#include <vector>

namespace velox::platform {

std::filesystem::path GetExecutablePath() {
  std::vector<wchar_t> buffer(MAX_PATH, L'\0');
  DWORD size = 0;
  while (true) {
    size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (size == 0) {
      return {};
    }
    if (size < buffer.size() - 1) {
      break;
    }
    buffer.resize(buffer.size() * 2);
  }
  return std::filesystem::path(std::wstring(buffer.data(), size));
}

std::filesystem::path GetExecutableDir() {
  return GetExecutablePath().parent_path();
}

bool EnsureDirectory(const std::filesystem::path& path) {
  if (path.empty()) {
    return false;
  }

  std::error_code error_code;
  if (std::filesystem::exists(path, error_code)) {
    return std::filesystem::is_directory(path, error_code);
  }

  return std::filesystem::create_directories(path, error_code);
}

std::filesystem::path MakeAbsolute(const std::filesystem::path& base,
                                   const std::filesystem::path& candidate) {
  if (candidate.empty()) {
    return {};
  }
  if (candidate.is_absolute()) {
    return candidate;
  }

  std::error_code error_code;
  const auto combined = std::filesystem::absolute(base / candidate, error_code);
  if (error_code) {
    return base / candidate;
  }
  return combined;
}

std::wstring ToWide(const std::string& value) {
  if (value.empty()) {
    return {};
  }

  const int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
  if (size <= 0) {
    return {};
  }

  std::wstring wide(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), wide.data(), size);
  return wide;
}

std::string ToUtf8(const std::wstring& value) {
  if (value.empty()) {
    return {};
  }

  const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
  if (size <= 0) {
    return {};
  }

  std::string utf8(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), utf8.data(), size, nullptr, nullptr);
  return utf8;
}

}  // namespace velox::platform
