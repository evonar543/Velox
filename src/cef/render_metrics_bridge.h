#pragma once

#include <string>

#include "include/cef_browser.h"
#include "include/cef_process_message.h"
#include "include/cef_v8.h"

namespace velox::cef {

constexpr char kRendererMetricMessageName[] = "Velox.RenderMetric";
constexpr char kRendererMetricFunctionName[] = "veloxEmitMetric";

class RenderMetricsBridge : public CefV8Handler {
 public:
  explicit RenderMetricsBridge(CefRefPtr<CefFrame> frame);

  bool Execute(const CefString& name,
               CefRefPtr<CefV8Value> object,
               const CefV8ValueList& arguments,
               CefRefPtr<CefV8Value>& retval,
               CefString& exception) override;

 private:
  CefRefPtr<CefFrame> frame_;

  IMPLEMENT_REFCOUNTING(RenderMetricsBridge);
};

void InstallRenderMetricsBridge(CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context);
bool TryReadRendererMetric(CefRefPtr<CefProcessMessage> message, std::string* name, double* value);

}  // namespace velox::cef
