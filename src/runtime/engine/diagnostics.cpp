#include "runtime/engine/diagnostics.hpp"

#include <algorithm>

#include "core/text.hpp"
#include "network/http_client.hpp"
#include "runtime/dom/dom_helpers.hpp"
#include "runtime/engine/js_handles.hpp"
#include "runtime/engine/script_context.hpp"
namespace webcli {
void trace_runtime(ScriptContext& state, const char* category,
                   const std::string& message) noexcept {
  try {
    // Retain recent metadata so observe --debug can diagnose earlier commands.
    // This bounds diagnostic storage, not page execution or network requests.
    if (state.diagnostic_trace.size() == 128) {
      state.diagnostic_trace.pop_front();
      ++state.diagnostic_dropped;
    }
    const auto elapsed = std::chrono::duration<double, std::milli>(
                             std::chrono::steady_clock::now() - state.time_origin)
                             .count();
    state.diagnostic_trace.push_back({elapsed, category, message.substr(0, 300)});
  } catch (...) {
    // Diagnostics must never replace a page exception or break an interaction.
  }
}
std::string diagnostic_url(const std::string& url) {
  const auto scheme = url.find("://");
  if (scheme == std::string::npos) {
    const auto colon = url.find(':');
    return colon == std::string::npos ? "non-http" : url.substr(0, colon + 1);
  }
  const auto start = url.find_first_of("/?#", scheme + 3);
  const auto end = url.find_first_of("?#", start);
  auto path =
      start != std::string::npos && url[start] == '/' ? url.substr(start, end - start) : "/";
  if (path.size() > 180) path = path.substr(0, 80) + "..." + path.substr(path.size() - 100);
  return url_origin(url) + path;
}
JSValue js_trace_input_event(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  if (argc < 4) return JS_UNDEFINED;
  const auto type = js_string(context, argv[0]);
  if (type != "click" && type != "mousedown" && type != "mouseup" && type != "keydown" &&
      type != "keyup" && type != "keypress" && type != "input" && type != "change" &&
      type != "blocked-disabled" && type != "blocked-inert" && type != "submit" &&
      type != "pointerdown" && type != "pointerup")
    return JS_UNDEFINED;
  int32_t node = 0, handlers = 0;
  if (JS_ToInt32(context, &node, argv[1]) < 0 || JS_ToInt32(context, &handlers, argv[2]) < 0)
    return JS_EXCEPTION;
  auto& state = *static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  trace_runtime(state, "event",
                type + " node=" + std::to_string(node) + " handlers=" + std::to_string(handlers) +
                    " prevented=" + (JS_ToBool(context, argv[3]) > 0 ? "true" : "false"));
  return JS_UNDEFINED;
}
namespace {
// Interrupt diagnostics must not call a page-defined getter or toString while
// the execution deadline is expired. QuickJS errors carry an own string stack.
std::string interrupted_stack(JSContext* context, JSValueConst exception) {
  if (!JS_IsObject(exception)) return {};
  const JSAtom atom = JS_NewAtom(context, "stack");
  JSPropertyDescriptor descriptor{};
  const int found = JS_GetOwnProperty(context, &descriptor, exception, atom);
  JS_FreeAtom(context, atom);
  std::string result;
  if (found > 0) {
    if (JS_IsString(descriptor.value)) {
      const char* stack = JS_ToCString(context, descriptor.value);
      if (stack) {
        result = truncate(stack, 1600);
        JS_FreeCString(context, stack);
      }
    }
    JS_FreeValue(context, descriptor.value);
    JS_FreeValue(context, descriptor.getter);
    JS_FreeValue(context, descriptor.setter);
  }
  if (JS_HasException(context)) JS_FreeValue(context, JS_GetException(context));
  return result;
}
}  // namespace
bool begin_js_task(ScriptContext* state) noexcept {
  JS_SetInterruptHandler(
      state->runtime,
      [](JSRuntime*, void* opaque) -> int {
        return static_cast<ExecutionBudget*>(opaque)->interrupt();
      },
      &state->execution);
  if (state->execution.begin_task()) return true;
  try {
    if (!state->execution.cancelled() && state->execution.report_window_timeout() &&
        state->errors && state->errors->size() < kMaxScriptErrors)
      state->errors->push_back("JS_OPERATION_TIMEOUT: page work exceeded the 60-second deadline");
  } catch (...) {
  }
  return false;
}
std::string js_error(JSContext* context, JSValueConst value) {
  const char* message = JS_ToCString(context, value);
  std::string result = message ? message : "JavaScript error";
  if (message) JS_FreeCString(context, message);
  if (JS_IsObject(value)) {
    JSValue stack_value = JS_GetPropertyStr(context, value, "stack");
    if (JS_IsString(stack_value)) {
      const char* stack = JS_ToCString(context, stack_value);
      if (stack && result.find(stack) == std::string::npos) result += "\n" + std::string(stack);
      if (stack) JS_FreeCString(context, stack);
    }
    JS_FreeValue(context, stack_value);
  }
  // A user-defined toString/stack getter can itself throw. Do not leave that
  // secondary exception pending for an unrelated task to consume later.
  if (JS_HasException(context)) JS_FreeValue(context, JS_GetException(context));
  // Long resource URLs must not consume the whole diagnostic before the source
  // line/column or following stack frames. Keep both ends of oversized lines.
  std::string compact;
  for (std::size_t start = 0; start < result.size();) {
    const auto newline = result.find('\n', start);
    const auto end = newline == std::string::npos ? result.size() : newline;
    const auto length = end - start;
    if (length > 420) {
      compact.append(result, start, 180);
      compact += "...";
      compact.append(result, end - 180, 180);
    } else
      compact.append(result, start, length);
    if (newline == std::string::npos) break;
    compact += '\n';
    start = newline + 1;
  }
  return truncate(compact, 1200);
}

void record_async_js_error(ScriptContext* state, JSContext* context) noexcept {
  JSValue exception = JS_GetException(context);
  try {
    const bool record = !state->execution.interrupted() || !state->execution.window_expired() ||
                        state->execution.report_window_timeout();
    if (record && state->errors && state->errors->size() < kMaxScriptErrors) {
      // Do not run exception stringification under an already expired deadline.
      // It can throw again and overwrite the original interrupt exception.
      auto detail =
          state->execution.interrupted()
              ? std::string(state->execution.window_expired()
                                ? "JS_OPERATION_TIMEOUT: exceeded the 60-second deadline"
                                : "JS_TASK_TIMEOUT: exceeded the 30-second execution deadline")
              : js_error(context, exception);
      if (state->execution.interrupted()) {
        const auto stack = interrupted_stack(context, exception);
        if (!stack.empty()) detail += "\n" + stack;
      }
      state->errors->push_back(state->error_context.empty() ? detail
                                                            : state->error_context + ": " + detail);
    }
  } catch (...) {
  }
  JS_FreeValue(context, exception);
}

void drain_timer_jobs(ScriptContext* state) {
  if (!JS_IsJobPending(state->runtime) || !begin_js_task(state)) return;
  for (unsigned int jobs = 0; jobs < 1000 && !state->execution.expired(); ++jobs) {
    JSContext* job_context = nullptr;
    const int result = JS_ExecutePendingJob(state->runtime, &job_context);
    if (result <= 0) {
      if (result < 0 && job_context)
        record_async_js_error(static_cast<ScriptContext*>(JS_GetContextOpaque(job_context)),
                              job_context);
      return;
    }
  }
}

// Defer reporting until the microtask checkpoint: catch handlers added in the
// same turn should suppress an unhandled-rejection diagnostic.
void promise_rejection_tracker(JSContext* context, JSValueConst promise, JSValueConst reason,
                               bool handled, void* opaque) noexcept {
  (void)opaque;
  auto& state = *static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  try {
    auto found = std::find_if(state.rejections.begin(), state.rejections.end(),
                              [&](const PendingRejection& entry) {
                                return JS_VALUE_GET_PTR(entry.promise) == JS_VALUE_GET_PTR(promise);
                              });
    if (handled) {
      if (found != state.rejections.end()) {
        JS_FreeValue(context, found->promise);
        JS_FreeValue(context, found->reason);
        state.rejections.erase(found);
      }
    } else if (found == state.rejections.end() && state.rejections.size() < kMaxScriptErrors) {
      state.rejections.push_back({JS_UNDEFINED, JS_UNDEFINED, state.error_context});
      state.rejections.back().promise = JS_DupValue(context, promise);
      state.rejections.back().reason = JS_DupValue(context, reason);
    }
  } catch (...) {
  }  // No exception may escape a QuickJS host callback.
}
void flush_promise_rejections(ScriptContext& state) noexcept {
  for (const auto& entry : state.rejections) {
    try {
      if (state.errors->size() < kMaxScriptErrors)
        state.errors->push_back(entry.origin + ": unhandled promise rejection: " +
                                js_error(state.context, entry.reason));
    } catch (...) {
    }
    JS_FreeValue(state.context, entry.promise);
    JS_FreeValue(state.context, entry.reason);
  }
  state.rejections.clear();
}
void clear_promise_rejections(ScriptContext& state) noexcept {
  for (const auto& entry : state.rejections) {
    JS_FreeValue(state.context, entry.promise);
    JS_FreeValue(state.context, entry.reason);
  }
  state.rejections.clear();
}
}  // namespace webcli
