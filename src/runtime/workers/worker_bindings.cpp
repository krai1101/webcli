#include "core/url/url.hpp"
#include "network/http_client.hpp"
#include "runtime/engine/js_handles.hpp"
#include "runtime/engine/script_context.hpp"
#include "runtime/network/resource_tasks.hpp"
#include "runtime/workers/worker_manager.hpp"
namespace webcli {
std::vector<uint8_t> serialize_worker_message(JSContext* context, JSValueConst value) {
  std::size_t size = 0;
  auto* buffer = JS_WriteObject(context, &size, value, JS_WRITE_OBJ_REFERENCE);
  if (!buffer) throw CliError("WORKER_CLONE", "worker message cannot be cloned");
  try {
    if (size > kBufferedBodyBytes)
      throw CliError("WORKER_CLONE", "worker message exceeds the buffered resource limit");
    std::vector<uint8_t> bytes(buffer, buffer + size);
    js_free(context, buffer);
    return bytes;
  } catch (...) {
    js_free(context, buffer);
    throw;
  }
}
JSValue js_worker_access(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  auto& state = *static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  if (!argc) return JS_ThrowTypeError(context, "worker operation required");
  const auto op = js_string(context, argv[0]);
  if (state.worker_channel) {
    auto& channel = *static_cast<WorkerChannel*>(state.worker_channel);
    if (op == "send" && argc > 2)
      channel.enqueue(false, {serialize_worker_message(context, argv[2]), {}});
    else if (op == "close")
      channel.stopped->store(true);
    else if (op == "import" && argc > 1) {
      if (channel.module)
        return JS_ThrowTypeError(context, "importScripts is unavailable in module workers");
      const auto url = resolve_web_url(state.base_url, js_string(context, argv[1]));
      if (!url) return JS_ThrowDOMException(context, "SyntaxError", "invalid worker import URL");
      HttpRequest request;
      request.url = *url;
      request.owner_cancelled = channel.stopped;
      // Classic worker imports use no-CORS script loading; credentials remain
      // restricted to the worker's inherited origin, including after redirects.
      request.cookie_origin = channel.origin;
      request.referrer_source = state.base_url;
      request.referrer_policy = state.referrer_policy;
      request.max_body_bytes = kMaxScriptBytes;
      const auto started = std::chrono::steady_clock::now();
      auto source = load_script_resource(request);
      state.execution.pause_for(std::chrono::steady_clock::now() - started);
      if (source.status < 200 || source.status >= 300)
        return JS_ThrowDOMException(context, "NetworkError", "worker import HTTP %ld",
                                    source.status);
      JSValue result = JS_Eval(context, source.body.data(), source.body.size(), source.url.c_str(),
                               JS_EVAL_TYPE_GLOBAL);
      if (JS_IsException(result)) return result;
      JS_FreeValue(context, result);
    } else
      return JS_ThrowTypeError(context, "invalid worker operation");
    return JS_UNDEFINED;
  }
  if (!state.workers) state.workers = std::make_shared<WorkerManager>(state);
  if (op == "create" && argc > 3) {
    try {
      return JS_NewInt32(context, state.workers->create(js_string(context, argv[1]),
                                                        js_string(context, argv[2]) == "module",
                                                        js_string(context, argv[3])));
    } catch (const CliError& error) {
      if (error.code == "WORKER_SECURITY")
        return JS_ThrowDOMException(context, "SecurityError", "%s", error.what());
      throw;
    }
  }
  int32_t id = 0;
  if (argc < 2) return JS_ThrowTypeError(context, "worker ID required");
  if (JS_ToInt32(context, &id, argv[1]) < 0) return JS_EXCEPTION;
  if (op == "send" && argc > 2)
    state.workers->send(id, serialize_worker_message(context, argv[2]));
  else if (op == "terminate")
    state.workers->terminate(id);
  else
    return JS_ThrowTypeError(context, "invalid worker operation");
  return JS_UNDEFINED;
}
}  // namespace webcli
