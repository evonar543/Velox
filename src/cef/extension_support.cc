#include "cef/extension_support.h"

#include <sstream>

namespace velox::cef {

namespace {

void AppendSwitchIfMissing(CefRefPtr<CefCommandLine> command_line, const wchar_t* name) {
  if (!command_line->HasSwitch(name)) {
    command_line->AppendSwitch(name);
  }
}

void AppendSwitchWithValueIfMissing(CefRefPtr<CefCommandLine> command_line,
                                    const wchar_t* name,
                                    const std::wstring& value) {
  if (!value.empty() && !command_line->HasSwitch(name)) {
    command_line->AppendSwitchWithValue(name, value);
  }
}

std::wstring JoinExtensionPaths(const std::vector<std::filesystem::path>& paths) {
  std::wostringstream stream;
  bool first = true;
  for (const auto& path : paths) {
    if (path.empty()) {
      continue;
    }
    if (!first) {
      stream << L",";
    }
    stream << path.wstring();
    first = false;
  }
  return stream.str();
}

}  // namespace

bool ExtensionsEnabled(const settings::AppSettings& settings) {
  return settings.extensions.enabled;
}

bool UseChromeRuntime(const settings::AppSettings& settings) {
  return settings.extensions.enabled && settings.extensions.chrome_runtime;
}

bool UseBarebonesPrototypeUi(const settings::AppSettings& settings) {
  return UseChromeRuntime(settings) && settings.ui.barebones_prototype;
}

std::wstring ResolveInitialBrowserUrl(const settings::AppSettings& settings) {
  if (UseChromeRuntime(settings) && settings.extensions.open_extensions_page_on_startup) {
    return L"chrome://extensions/";
  }
  return settings.startup_url;
}

void ApplyExtensionSwitches(CefRefPtr<CefCommandLine> command_line, const settings::AppSettings& settings) {
  if (!settings.extensions.enabled) {
    return;
  }

  // Velox keeps extension loading to unpacked directories for now. That gives
  // us a predictable prototype path and makes local extension iteration easy.
  const std::wstring unpacked_paths = JoinExtensionPaths(settings.extensions.unpacked_dirs);
  AppendSwitchWithValueIfMissing(command_line, L"load-extension", unpacked_paths);
  AppendSwitchWithValueIfMissing(command_line, L"disable-extensions-except", unpacked_paths);

  if (settings.extensions.allow_file_access) {
    AppendSwitchIfMissing(command_line, L"allow-file-access-from-files");
  }

  for (const auto& raw_switch : settings.extensions.extra_chromium_switches) {
    if (raw_switch.empty()) {
      continue;
    }

    const size_t equals_pos = raw_switch.find(L'=');
    if (equals_pos == std::wstring::npos) {
      AppendSwitchIfMissing(command_line, raw_switch.c_str());
      continue;
    }

    const std::wstring name = raw_switch.substr(0, equals_pos);
    const std::wstring value = raw_switch.substr(equals_pos + 1);
    AppendSwitchWithValueIfMissing(command_line, name.c_str(), value);
  }
}

}  // namespace velox::cef
