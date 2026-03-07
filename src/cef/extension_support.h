#pragma once

#include <string>

#include "include/cef_command_line.h"
#include "settings/app_settings.h"

namespace velox::cef {

bool ExtensionsEnabled(const settings::AppSettings& settings);
bool UseChromeRuntime(const settings::AppSettings& settings);
bool UseBarebonesPrototypeUi(const settings::AppSettings& settings);
std::wstring ResolveInitialBrowserUrl(const settings::AppSettings& settings);
void ApplyExtensionSwitches(CefRefPtr<CefCommandLine> command_line, const settings::AppSettings& settings);

}  // namespace velox::cef
