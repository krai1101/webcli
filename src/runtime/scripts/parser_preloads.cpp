#include "runtime/scripts/parser_preloads.hpp"

#include <chrono>
#include <thread>

#include "core/errors.hpp"
#include "runtime/engine/diagnostics.hpp"
#include "runtime/engine/js_handles.hpp"
#include "runtime/engine/js_runtime.hpp"
#include "runtime/engine/script_context.hpp"

namespace webcli {
std::string await_parser_source(JsRuntime& runtime, ScriptContext& state, int node_id) {
  const auto expression =
      "(()=>{const node=__wrap(" + std::to_string(node_id) +
      ");const pending=node._parserResource;node._parserResource=null;return pending;})()";
  JsValueOwner promise(state.context, JS_Eval(state.context, expression.data(), expression.size(),
                                              "parser preload", JS_EVAL_TYPE_GLOBAL));
  if (JS_IsException(promise.get())) {
    record_async_js_error(&state, state.context);
    throw CliError("SCRIPT_PRELOAD", "could not access parser resource");
  }
  if (JS_PromiseState(state.context, promise.get()) == JS_PROMISE_NOT_A_PROMISE)
    throw CliError("SCRIPT_PRELOAD", "parser resource is not a promise");
  while (JS_PromiseState(state.context, promise.get()) == JS_PROMISE_PENDING) {
    if (state.execution.cancelled())
      throw CliError("REQUEST_CANCELLED", "script loading cancelled");
    state.script_loading.request_timeout();
    if (state.execution.window_expired())
      throw CliError("SCRIPT_TIMEOUT", "parser resource exceeded the document load deadline");
    runtime.pump();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  JsValueOwner result(state.context, JS_PromiseResult(state.context, promise.get()));
  if (JS_PromiseState(state.context, promise.get()) == JS_PROMISE_REJECTED)
    throw CliError("SCRIPT_PRELOAD", js_error(state.context, result.get()));
  JsValueOwner response(state.context, JS_GetPropertyStr(state.context, result.get(), "response"));
  if (JS_IsException(response.get())) {
    const JsValueOwner error(state.context, JS_GetException(state.context));
    throw CliError("SCRIPT_PRELOAD", js_error(state.context, error.get()));
  }
  if (JS_IsUndefined(response.get())) {
    JsValueOwner error(state.context, JS_GetPropertyStr(state.context, result.get(), "error"));
    if (JS_IsException(error.get())) {
      const JsValueOwner exception(state.context, JS_GetException(state.context));
      throw CliError("SCRIPT_PRELOAD", js_error(state.context, exception.get()));
    }
    throw CliError("SCRIPT_PRELOAD", js_error(state.context, error.get()));
  }
  return property_string(state.context, response.get(), "body");
}
}  // namespace webcli
