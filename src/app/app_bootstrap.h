#pragma once

#include <Windows.h>

#include <memory>

#include "app/command_line.h"
#include "app/runtime_profile.h"
#include "browser/browser_window.h"
#include "cef/velox_cef_app.h"
#include "profiling/metrics_recorder.h"
#include "settings/app_settings.h"

namespace velox::app {

class AppBootstrap {
 public:
  AppBootstrap(HINSTANCE instance, int command_show);
  int Run();

 private:
  bool InitializeDiagnostics();
  bool InitializeSettings();
  bool InitializeCef(const CefMainArgs& main_args);
  int RunMessageLoop();
  void Shutdown();

  HINSTANCE instance_ = nullptr;
  int command_show_ = SW_SHOWNORMAL;

  CommandLineOptions command_line_;
  settings::AppSettings settings_;
  RuntimeProfile runtime_profile_;
  profiling::MetricsRecorder metrics_;
  std::unique_ptr<browser::BrowserWindow> browser_window_;
  CefRefPtr<cef::VeloxCefApp> cef_app_;
  bool cef_initialized_ = false;
  bool shut_down_ = false;
};

}  // namespace velox::app
