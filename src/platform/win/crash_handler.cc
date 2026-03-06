#include "platform/win/crash_handler.h"

#include <Windows.h>
#include <DbgHelp.h>
#include <crtdbg.h>

#include <atomic>
#include <filesystem>
#include <sstream>

#include "platform/win/file_utils.h"
#include "platform/win/logger.h"

namespace velox::platform {

namespace {

std::filesystem::path g_dump_directory;
std::atomic<bool> g_handler_installed = false;

std::filesystem::path BuildDumpPath() {
  SYSTEMTIME system_time{};
  GetLocalTime(&system_time);

  std::wstringstream stream;
  stream << L"velox-crash-"
         << system_time.wYear
         << system_time.wMonth
         << system_time.wDay
         << L'-'
         << system_time.wHour
         << system_time.wMinute
         << system_time.wSecond
         << L".dmp";
  return g_dump_directory / stream.str();
}

}  // namespace

void CrashHandler::Install(const std::filesystem::path& dump_directory) {
  g_dump_directory = dump_directory;
  EnsureDirectory(g_dump_directory);
  if (g_handler_installed.exchange(true)) {
    return;
  }

  SetUnhandledExceptionFilter(&CrashHandler::HandleException);
  _set_invalid_parameter_handler(&CrashHandler::HandleInvalidParameter);
  std::set_terminate(&CrashHandler::HandleTerminate);
}

long __stdcall CrashHandler::HandleException(EXCEPTION_POINTERS* exception_pointers) {
  LogError("Unhandled exception captured. Writing minidump.");
  WriteMinidump(exception_pointers);
  return EXCEPTION_EXECUTE_HANDLER;
}

void CrashHandler::HandleTerminate() {
  LogError("std::terminate invoked. Aborting process.");
  RaiseFailFastException(nullptr, nullptr, 0);
}

void __cdecl CrashHandler::HandleInvalidParameter(const wchar_t* expression,
                                                  const wchar_t* function,
                                                  const wchar_t* file,
                                                  unsigned int line,
                                                  uintptr_t reserved) {
  (void)reserved;
  std::ostringstream stream;
  stream << "Invalid parameter handler triggered at line " << line;
  if (expression != nullptr) {
    stream << " expression=" << ToUtf8(expression);
  }
  if (function != nullptr) {
    stream << " function=" << ToUtf8(function);
  }
  if (file != nullptr) {
    stream << " file=" << ToUtf8(file);
  }
  LogError(stream.str());
  RaiseFailFastException(nullptr, nullptr, 0);
}

void CrashHandler::WriteMinidump(EXCEPTION_POINTERS* exception_pointers) {
  const std::filesystem::path dump_path = BuildDumpPath();
  const HANDLE file_handle = CreateFileW(dump_path.c_str(),
                                         GENERIC_WRITE,
                                         0,
                                         nullptr,
                                         CREATE_ALWAYS,
                                         FILE_ATTRIBUTE_NORMAL,
                                         nullptr);
  if (file_handle == INVALID_HANDLE_VALUE) {
    LogError("Failed to create minidump file.");
    return;
  }

  MINIDUMP_EXCEPTION_INFORMATION exception_info{};
  exception_info.ThreadId = GetCurrentThreadId();
  exception_info.ExceptionPointers = exception_pointers;
  exception_info.ClientPointers = FALSE;

  const BOOL result = MiniDumpWriteDump(GetCurrentProcess(),
                                        GetCurrentProcessId(),
                                        file_handle,
                                        static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory),
                                        exception_pointers ? &exception_info : nullptr,
                                        nullptr,
                                        nullptr);
  CloseHandle(file_handle);

  if (!result) {
    LogError("MiniDumpWriteDump failed.");
    return;
  }

  LogInfo("Crash dump written to " + ToUtf8(dump_path.wstring()));
}

}  // namespace velox::platform
