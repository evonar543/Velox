#pragma once

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

namespace velox::platform {

enum class LogLevel {
  kTrace = 0,
  kInfo = 1,
  kWarning = 2,
  kError = 3,
};

class Logger {
 public:
  static Logger& Instance();

  bool Initialize(const std::filesystem::path& log_path, LogLevel minimum_level);
  void Shutdown();
  void Log(LogLevel level, std::string_view message);

  std::filesystem::path log_path() const;
  LogLevel minimum_level() const;

 private:
  Logger() = default;

  std::string FormatLine(LogLevel level, std::string_view message) const;

  mutable std::mutex mutex_;
  std::ofstream stream_;
  std::filesystem::path log_path_;
  LogLevel minimum_level_ = LogLevel::kInfo;
};

LogLevel LogLevelFromString(const std::string& value);
const char* LogLevelToString(LogLevel level);

void LogTrace(std::string_view message);
void LogInfo(std::string_view message);
void LogWarning(std::string_view message);
void LogError(std::string_view message);

}  // namespace velox::platform
