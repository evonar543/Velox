#include "app/command_line.h"

#include <Windows.h>
#include <shellapi.h>

namespace velox::app {

namespace {

bool TryReadValue(const std::wstring& argument,
                  const std::wstring& prefix,
                  std::optional<std::wstring>* output) {
  if (argument.rfind(prefix, 0) != 0) {
    return false;
  }
  *output = argument.substr(prefix.size());
  return true;
}

bool IsSubprocessArgument(const std::wstring& argument) {
  return argument.rfind(L"--type=", 0) == 0;
}

}  // namespace

CommandLineOptions ParseCommandLine() {
  CommandLineOptions options;

  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (argv == nullptr) {
    return options;
  }

  for (int index = 1; index < argc; ++index) {
    if (IsSubprocessArgument(argv[index])) {
      LocalFree(argv);
      return options;
    }
  }

  for (int index = 1; index < argc; ++index) {
    const std::wstring argument = argv[index];
    if (argument == L"--incognito") {
      options.incognito = true;
      continue;
    }
    if (argument == L"--enable-extensions") {
      options.enable_extensions = true;
      continue;
    }
    if (argument == L"--barebones-ui") {
      options.barebones_ui = true;
      continue;
    }
    if (argument == L"--extensions-page") {
      options.open_extensions_page = true;
      continue;
    }
    if (argument == L"--quit-after-load") {
      options.quit_after_load = true;
      continue;
    }
    if (TryReadValue(argument, L"--url=", &options.startup_url)) {
      continue;
    }
    if (TryReadValue(argument, L"--profile-dir=", &options.profile_dir)) {
      continue;
    }
    if (TryReadValue(argument, L"--log-file=", &options.log_file)) {
      continue;
    }
    if (TryReadValue(argument, L"--dump-benchmarks=", &options.benchmark_output)) {
      continue;
    }
    std::optional<std::wstring> extension_dir;
    if (TryReadValue(argument, L"--extension-dir=", &extension_dir) && extension_dir.has_value()) {
      options.enable_extensions = true;
      options.extension_dirs.push_back(*extension_dir);
      continue;
    }
  }

  LocalFree(argv);
  return options;
}

}  // namespace velox::app
