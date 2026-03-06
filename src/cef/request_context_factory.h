#pragma once

#include "include/cef_request_context.h"
#include "include/cef_request_context_handler.h"
#include "profiling/metrics_recorder.h"
#include "settings/app_settings.h"

namespace velox::cef {

CefRefPtr<CefRequestContext> CreateRequestContext(const settings::AppSettings& settings,
                                                  bool incognito,
                                                  profiling::MetricsRecorder* metrics);

}  // namespace velox::cef
