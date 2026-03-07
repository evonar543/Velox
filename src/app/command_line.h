#pragma once

#include <optional>
#include <string>
#include <vector>

namespace velox::app {

struct CommandLineOptions {
  std::optional<std::wstring> startup_url;
  bool incognito = false;
  bool quit_after_load = false;
  bool enable_extensions = false;
  bool barebones_ui = false;
  bool open_extensions_page = false;
  std::optional<std::wstring> profile_dir;
  std::optional<std::wstring> log_file;
  std::optional<std::wstring> benchmark_output;
  std::vector<std::wstring> extension_dirs;
};

CommandLineOptions ParseCommandLine();

}  // namespace velox::app
