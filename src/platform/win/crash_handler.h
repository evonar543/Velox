#pragma once

#include <filesystem>

namespace velox::platform {

class CrashHandler {
 public:
  static void Install(const std::filesystem::path& dump_directory);

 private:
  static long __stdcall HandleException(struct _EXCEPTION_POINTERS* exception_pointers);
  static void HandleTerminate();
  static void __cdecl HandleInvalidParameter(const wchar_t* expression,
                                             const wchar_t* function,
                                             const wchar_t* file,
                                             unsigned int line,
                                             uintptr_t reserved);
  static void WriteMinidump(struct _EXCEPTION_POINTERS* exception_pointers);
};

}  // namespace velox::platform
