#include "runtime/events/timers.hpp"

#include <limits>

#include "runtime/engine/diagnostics.hpp"
#include "runtime/engine/js_handles.hpp"
#include "runtime/engine/script_context.hpp"
namespace webcli {
namespace {
JSValue run_microtask(JSContext* context, int, JSValueConst* arguments) {
  return JS_Call(context, arguments[0], JS_UNDEFINED, 0, nullptr);
}
}  // namespace

JSValue js_native_queue_microtask(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  if (!argc || !JS_IsFunction(context, argv[0]))
    return JS_ThrowTypeError(context, "queueMicrotask requires a callback function");
  // EnqueueJob retains the callback. No Promise constructor, prototype, or
  // species hook controlled by page code participates in native scheduling.
  if (JS_EnqueueJob(context, run_microtask, 1, argv) < 0) return JS_EXCEPTION;
  return JS_UNDEFINED;
}

JSValue js_performance_now(JSContext* context, JSValueConst, int, JSValueConst*) {
  const auto& state = *static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  const auto elapsed = std::chrono::steady_clock::now() - state.time_origin;
  return JS_NewFloat64(context, std::chrono::duration<double, std::milli>(elapsed).count());
}

JSValue js_native_set_timeout(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  if (argc < 1 || !JS_IsFunction(context, argv[0]))
    return JS_ThrowTypeError(context, "setTimeout requires a callback function");
  auto* state = static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  if (state->timers.size() >= kActiveTimers)
    return JS_ThrowInternalError(context, "page exceeded %zu active timers", kActiveTimers);
  int64_t delay = 0;
  if (argc > 1 && JS_ToInt64(context, &delay, argv[1]) < 0) return JS_EXCEPTION;
  if (delay < 0) delay = 0;
  if (delay > kTimerDelayMs) delay = kTimerDelayMs;
  if (state->next_timer_id == std::numeric_limits<int>::max())
    return JS_ThrowInternalError(context, "timer ID sequence exhausted");
  const int id = state->next_timer_id++;
  auto timer = std::make_unique<JsTimer>();
  timer->callback = JS_DupValue(context, argv[0]);
  try {
    timer->task = state->scheduler.schedule(
        TaskScheduler::Source::Timer, std::chrono::milliseconds(delay), [state, id] {
          const auto found = state->timers.find(id);
          if (found == state->timers.end()) return;
          std::unique_ptr<JsTimer> fired(found->second);
          state->timers.erase(found);
          const JsValueOwner callback(state->context, fired->callback);
          fired->callback = JS_UNDEFINED;
          const JsValueOwner result(
              state->context, JS_Call(state->context, callback.get(), JS_UNDEFINED, 0, nullptr));
          if (JS_IsException(result.get())) record_async_js_error(state, state->context);
        });
    state->timers.emplace(id, timer.get());
  } catch (...) {
    state->scheduler.cancel(timer->task);
    JS_FreeValue(context, timer->callback);
    throw;
  }
  timer.release();
  return JS_NewInt32(context, id);
}

JSValue js_native_clear_timeout(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  if (argc < 1) return JS_UNDEFINED;
  auto* state = static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  int32_t id = 0;
  if (JS_ToInt32(context, &id, argv[0]) < 0) return JS_EXCEPTION;
  const auto found = state->timers.find(id);
  if (found == state->timers.end()) return JS_UNDEFINED;
  auto* timer = found->second;
  state->timers.erase(found);
  state->scheduler.cancel(timer->task);
  JS_FreeValue(context, timer->callback);
  timer->callback = JS_UNDEFINED;
  delete timer;
  return JS_UNDEFINED;
}

}  // namespace webcli
