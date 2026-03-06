#include "cef/render_metrics_bridge.h"

namespace velox::cef {

namespace {

const char kPaintObserverScript[] = R"(
(() => {
  try {
    if (window.__veloxPaintObserverInstalled) {
      return;
    }
    window.__veloxPaintObserverInstalled = true;
    const seen = new Set();
    const emit = (name, value) => {
      if (typeof veloxEmitMetric === 'function') {
        veloxEmitMetric(String(name), Number(value) || 0);
      }
    };
    const flushExisting = () => {
      for (const entry of performance.getEntriesByType('paint')) {
        if (!seen.has(entry.name)) {
          seen.add(entry.name);
          emit(entry.name, entry.startTime);
        }
      }
    };
    flushExisting();
    if (window.PerformanceObserver) {
      const observer = new PerformanceObserver((list) => {
        for (const entry of list.getEntries()) {
          if (!seen.has(entry.name)) {
            seen.add(entry.name);
            emit(entry.name, entry.startTime);
          }
        }
      });
      observer.observe({ type: 'paint', buffered: true });
    }
  } catch (_) {
  }
})();
)";

}  // namespace

RenderMetricsBridge::RenderMetricsBridge(CefRefPtr<CefFrame> frame) : frame_(frame) {}

bool RenderMetricsBridge::Execute(const CefString& name,
                                  CefRefPtr<CefV8Value> object,
                                  const CefV8ValueList& arguments,
                                  CefRefPtr<CefV8Value>& retval,
                                  CefString& exception) {
  (void)object;
  (void)retval;
  if (name != kRendererMetricFunctionName || arguments.size() < 2 || frame_ == nullptr) {
    return false;
  }
  if (!arguments[0]->IsString() || (!arguments[1]->IsDouble() && !arguments[1]->IsInt() && !arguments[1]->IsUInt())) {
    exception = "veloxEmitMetric expects (string, number)";
    return true;
  }

  auto message = CefProcessMessage::Create(kRendererMetricMessageName);
  auto list = message->GetArgumentList();
  list->SetString(0, arguments[0]->GetStringValue());
  list->SetDouble(1, arguments[1]->GetDoubleValue());
  frame_->SendProcessMessage(PID_BROWSER, message);
  return true;
}

void InstallRenderMetricsBridge(CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context) {
  if (frame == nullptr || context == nullptr || !frame->IsMain()) {
    return;
  }

  // Paint timing lives in Chromium's renderer, so we bridge it back to the
  // browser process instead of guessing from the native shell.
  auto handler = new RenderMetricsBridge(frame);
  auto function = CefV8Value::CreateFunction(kRendererMetricFunctionName, handler);
  auto global = context->GetGlobal();
  global->SetValue(kRendererMetricFunctionName,
                   function,
                   static_cast<cef_v8_propertyattribute_t>(
                       V8_PROPERTY_ATTRIBUTE_READONLY | V8_PROPERTY_ATTRIBUTE_DONTDELETE));
  frame->ExecuteJavaScript(kPaintObserverScript, frame->GetURL(), 0);
}

bool TryReadRendererMetric(CefRefPtr<CefProcessMessage> message, std::string* name, double* value) {
  if (message == nullptr || message->GetName() != kRendererMetricMessageName) {
    return false;
  }

  auto list = message->GetArgumentList();
  if (list == nullptr || list->GetSize() < 2) {
    return false;
  }

  *name = list->GetString(0).ToString();
  *value = list->GetDouble(1);
  return true;
}

}  // namespace velox::cef
