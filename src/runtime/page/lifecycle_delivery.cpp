#include "runtime/page/lifecycle_delivery.hpp"

#include <algorithm>

#include "runtime/engine/diagnostics.hpp"
#include "runtime/engine/js_handles.hpp"
#include "runtime/engine/js_runtime.hpp"
#include "runtime/engine/script_context.hpp"
#include "runtime/frames/frame_tree.hpp"

namespace webcli {
JSValue js_document_readiness(JSContext* context, JSValueConst, int, JSValueConst*) {
  const auto& state = *static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  return JS_NewString(context, state.lifecycle.ready_state());
}
void advance_document_lifecycle(JsRuntime& runtime, ScriptContext& state) {
  state.frame_tree->scan(state);
  if (!begin_js_task(&state) || JS_IsJobPending(state.runtime)) return;
  state.css.stylesheet_resources.poll(state);
  const JsValueOwner global(state.context, JS_GetGlobalObject(state.context));
  if (state.navigation) {
    state.lifecycle.abort();
    return;
  }
  const bool deferred_pending = std::any_of(
      state.pending_modules.begin(), state.pending_modules.end(), [&](const PendingModule& module) {
        return module.blocks_content_loaded &&
               JS_PromiseState(state.context, module.promise) == JS_PROMISE_PENDING;
      });
  if (state.lifecycle.claim_content_loaded(deferred_pending))
    runtime.evaluate("document.dispatchEvent(new Event('DOMContentLoaded',{bubbles:true}));",
                     "document content loaded");
  if (state.navigation) {
    state.lifecycle.abort();
    return;
  }
  if (JS_IsJobPending(state.runtime)) return;
  const JsValueOwner value(
      state.context, JS_GetPropertyStr(state.context, global.get(), "__webcliPendingScriptLoads"));
  int32_t async_pending = 0;
  if (JS_ToInt32(state.context, &async_pending, value.get()) < 0) {
    record_async_js_error(&state, state.context);
    return;
  }
  const bool module_pending = std::any_of(
      state.pending_modules.begin(), state.pending_modules.end(), [&](const PendingModule& module) {
        return JS_PromiseState(state.context, module.promise) == JS_PROMISE_PENDING;
      });
  if (state.execution.window_expired()) return;
  if (state.lifecycle.claim_load(async_pending > 0 || module_pending ||
                                 state.css.stylesheet_resources.pending()))
    runtime.evaluate(
        "document.dispatchEvent(new Event('readystatechange'));"
        "window.dispatchEvent(new Event('load'));",
        "document load complete");
}
}  // namespace webcli
