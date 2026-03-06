#pragma once

#include <optional>
#include <string>

namespace velox::app {

struct CommandLineOptions {
  std::optional<std::wstring> startup_url;
  bool incognito = false;
  bool quit_after_load = false;
  std::optional<std::wstring> profile_dir;
  std::optional<std::wstring> log_file;
  std::optional<std::wstring> benchmark_output;
};

CommandLineOptions ParseCommandLine();

}  // namespace velox::app
