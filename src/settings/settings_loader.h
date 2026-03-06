#pragma once

#include <filesystem>

#include "app/command_line.h"
#include "settings/app_settings.h"

namespace velox::settings {

AppSettings LoadSettings(const std::filesystem::path& config_path,
                         const std::filesystem::path& base_dir,
                         const app::CommandLineOptions& command_line);

}  // namespace velox::settings
