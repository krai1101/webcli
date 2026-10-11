#include "runtime/network/completion_delivery.hpp"

#include "runtime/engine/diagnostics.hpp"
#include "runtime/engine/js_handles.hpp"
#include "runtime/engine/script_context.hpp"

namespace webcli {
namespace {
bool set(JSContext* context, JSValueConst object, const char* name, JSValue value) {
  if (JS_IsException(value)) return false;
  return JS_SetPropertyStr(context, object, name, value) >= 0;
}

JSValue make_response(JSContext* context, const HttpResponse& response, const FetchJob& job) {
  JsValueOwner result(context, JS_NewObject(context));
  if (JS_IsException(result.get())) return JS_EXCEPTION;
  if (job.opaque) {
    if (!set(context, result.get(), "loaded",
             JS_NewBool(context, response.status >= 200 && response.status < 300 &&
                                     response.content_type.rfind("image/", 0) == 0)))
      return JS_EXCEPTION;
    return result.release();
  }
  if (response.opaque) {
    if (!set(context, result.get(), "opaque", JS_NewBool(context, true)) ||
        !set(context, result.get(), "type",
             JS_NewString(context,
                          response.content_type == "opaqueredirect" ? "opaqueredirect" : "opaque")))
      return JS_EXCEPTION;
    return result.release();
  }
  if (!set(context, result.get(), "status",
           JS_NewInt32(context, static_cast<int>(response.status))) ||
      !set(context, result.get(), "url", JS_NewString(context, response.url.c_str())))
    return JS_EXCEPTION;
  const auto body =
      job.script
          ? JS_NewStringLen(context, response.body.data(), response.body.size())
          : JS_NewArrayBufferCopy(context, reinterpret_cast<const uint8_t*>(response.body.data()),
                                  response.body.size());
  if (!set(context, result.get(), job.script ? "body" : "bytes", body)) return JS_EXCEPTION;
  JsValueOwner headers(context, JS_NewObject(context));
  if (JS_IsException(headers.get())) return JS_EXCEPTION;
  for (const auto& pair : response.headers)
    if (pair.first != "set-cookie" && pair.first != "set-cookie2" &&
        !set(context, headers.get(), pair.first.c_str(),
             JS_NewStringLen(context, pair.second.data(), pair.second.size())))
      return JS_EXCEPTION;
  if (!set(context, result.get(), "headers", headers.release())) return JS_EXCEPTION;
  return result.release();
}
}  // namespace

bool deliver_resource_completion(ScriptContext& state, FetchJob& job) {
  // Acquire the task before consuming the future. A turn that has exhausted its
  // budget leaves the completion and both promise callbacks owned by the queue.
  if (!begin_js_task(&state)) return false;
  JSValue callback = job.resolve;
  JSValue value;
  try {
    auto response = job.future.get();
    if (job.script) state.modules->received_script(job.script_url, response);
    value = make_response(state.context, response, job);
  } catch (const std::exception& error) {
    const auto* failure = dynamic_cast<const CliError*>(&error);
    const auto message = failure ? failure->code + ": " + error.what() : std::string(error.what());
    value = JS_NewError(state.context);
    if (!JS_IsException(value) &&
        !set(state.context, value, "message", JS_NewString(state.context, message.c_str()))) {
      JS_FreeValue(state.context, value);
      value = JS_EXCEPTION;
    }
    callback = job.reject;
  }
  if (JS_IsException(value)) {
    value = JS_GetException(state.context);
    callback = job.reject;
  }
  JsValueOwner argument(state.context, value);
  // Promise capability callbacks settle synchronously; reactions are queued.
  // Never check the deadline again between consuming a future and settlement.
  JsValueOwner result(state.context, JS_Call(state.context, callback, JS_UNDEFINED, 1, &value));
  if (JS_IsException(result.get())) {
    record_async_js_error(&state, state.context);
    throw CliError("RESOURCE_DELIVERY", "could not settle resource promise");
  }
  return true;
}
namespace {
void enqueue_delivery(ScriptContext& state, std::shared_ptr<FetchJob> completion) {
  state.scheduler.schedule(TaskScheduler::Source::Resource, TaskScheduler::Clock::duration::zero(),
                           [&state, completion = std::move(completion)] {
                             if (!deliver_resource_completion(state, *completion))
                               enqueue_delivery(state, completion);
                           });
}
}  // namespace
void schedule_resource_completion(ScriptContext& state, std::unique_ptr<FetchJob> completion) {
  enqueue_delivery(state, std::shared_ptr<FetchJob>(std::move(completion)));
}
}  // namespace webcli
