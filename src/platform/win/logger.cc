#include "platform/win/logger.h"

#include <Windows.h>

#include <iomanip>
#include <sstream>

#include "platform/win/file_utils.h"

namespace velox::platform {

namespace {

std::string TimestampNow() {
  SYSTEMTIME system_time{};
  GetLocalTime(&system_time);

  std::ostringstream stream;
  stream << std::setfill('0')
         << std::setw(4) << system_time.wYear << '-'
         << std::setw(2) << system_time.wMonth << '-'
         << std::setw(2) << system_time.wDay << ' '
         << std::setw(2) << system_time.wHour << ':'
         << std::setw(2) << system_time.wMinute << ':'
         << std::setw(2) << system_time.wSecond << '.'
         << std::setw(3) << system_time.wMilliseconds;
  return stream.str();
}

}  // namespace

Logger& Logger::Instance() {
  static Logger instance;
  return instance;
}

bool Logger::Initialize(const std::filesystem::path& log_path, LogLevel minimum_level) {
  std::scoped_lock lock(mutex_);

  if (log_path.has_parent_path()) {
    EnsureDirectory(log_path.parent_path());
  }

  stream_.close();
  stream_.open(log_path, std::ios::out | std::ios::app);
  if (!stream_.is_open()) {
    return false;
  }

  log_path_ = log_path;
  minimum_level_ = minimum_level;
  stream_.setf(std::ios::unitbuf);
  return true;
}

void Logger::Shutdown() {
  std::scoped_lock lock(mutex_);
  stream_.flush();
  stream_.close();
}

void Logger::Log(LogLevel level, std::string_view message) {
  if (level < minimum_level_) {
    return;
  }

  const std::string line = FormatLine(level, message);
  {
    std::scoped_lock lock(mutex_);
    if (stream_.is_open()) {
      stream_ << line << "\n";
      stream_.flush();
    }
  }

  const std::wstring wide = ToWide(line + "\n");
  OutputDebugStringW(wide.c_str());
}

std::filesystem::path Logger::log_path() const {
  std::scoped_lock lock(mutex_);
  return log_path_;
}

LogLevel Logger::minimum_level() const {
  std::scoped_lock lock(mutex_);
  return minimum_level_;
}

std::string Logger::FormatLine(LogLevel level, std::string_view message) const {
  std::ostringstream stream;
  stream << '[' << TimestampNow() << "] [" << LogLevelToString(level) << "] " << message;
  return stream.str();
}

LogLevel LogLevelFromString(const std::string& value) {
  if (value == "trace") {
    return LogLevel::kTrace;
  }
  if (value == "warning") {
    return LogLevel::kWarning;
  }
  if (value == "error") {
    return LogLevel::kError;
  }
  return LogLevel::kInfo;
}

const char* LogLevelToString(LogLevel level) {
  switch (level) {
    case LogLevel::kTrace:
      return "trace";
    case LogLevel::kInfo:
      return "info";
    case LogLevel::kWarning:
      return "warning";
    case LogLevel::kError:
      return "error";
  }
  return "info";
}

void LogTrace(std::string_view message) {
  Logger::Instance().Log(LogLevel::kTrace, message);
}

void LogInfo(std::string_view message) {
  Logger::Instance().Log(LogLevel::kInfo, message);
}

void LogWarning(std::string_view message) {
  Logger::Instance().Log(LogLevel::kWarning, message);
}

void LogError(std::string_view message) {
  Logger::Instance().Log(LogLevel::kError, message);
}

}  // namespace velox::platform
