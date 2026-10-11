#include "runtime/network/resource_tasks.hpp"

#include "core/script_types.hpp"
#include "network/data_script.hpp"
#include "network/resource_service.hpp"
#include "runtime/dom/dom_bindings.hpp"
#include "runtime/dom/dom_helpers.hpp"
#include "runtime/engine/js_handles.hpp"
#include "runtime/engine/script_context.hpp"
#include "runtime/network/referrer_policy.hpp"

namespace webcli {
namespace {
HttpResponse load_resource(const HttpRequest& request, bool script) {
  auto response =
      script && is_data_script_url(request.url)
          ? HttpResponse{200, request.url, decode_data_script(request.url), "text/javascript", {},
                         {},  {}}
          : perform_http(request);
  if (script && (response.status < 200 || response.status >= 300))
    throw CliError("SCRIPT_HTTP_STATUS",
                   "script returned HTTP " + std::to_string(response.status) + ": " + response.url);
  if (script) {
    const auto type = script_type_essence(response.content_type);
    const auto nosniff = response.headers.find("x-content-type-options");
    const bool javascript = !type.empty() && script_kind(type) == ScriptKind::Classic;
    const bool blocked = type.rfind("image/", 0) == 0 || type.rfind("audio/", 0) == 0 ||
                         type.rfind("video/", 0) == 0 || type == "text/csv";
    if (blocked || (nosniff != response.headers.end() &&
                    script_type_essence(nosniff->second) == "nosniff" && !javascript))
      throw CliError("SCRIPT_CONTENT_TYPE",
                     "refused script content type " + type + ": " + response.url);
  }
  return response;
}
}  // namespace
HttpResponse load_script_resource(const HttpRequest& request) { return load_resource(request, true); }
void start_queued_http(ScriptContext& state) {
  std::size_t active = 0;
  for (const auto& job : state.network.fetches)
    if (job->future.valid()) ++active;
  for (const auto& job : state.network.fetches) {
    if (active >= kResourceConcurrency) break;
    if (!job->queued_request) continue;
    auto request = std::move(*job->queued_request);
    request.deadline_start = std::chrono::steady_clock::now();
    const bool script = job->script;
    try {
      job->future = resource_service().submit(
          [request = std::move(request), script] { return load_resource(request, script); });
    } catch (...) {
      // Deliver launch failures through the same promise completion path.
      std::promise<HttpResponse> failed;
      failed.set_exception(std::current_exception());
      job->future = failed.get_future();
    }
    job->queued_request.reset();
    ++active;
  }
}
JSValue enqueue_http(ScriptContext& state, HttpRequest request, bool script, bool opaque) {
  if (state.network.fetches.size() >= kResourceQueueSize)
    return JS_ThrowInternalError(state.context, "page exceeded 256 queued resource requests");
  auto job = std::make_unique<FetchJob>();
  job->id = state.network.next_http_id++;
  JSValue callbacks[2];
  JsValueOwner promise(state.context, JS_NewPromiseCapability(state.context, callbacks));
  if (JS_IsException(promise.get())) return promise.release();
  JS_DefinePropertyValueStr(state.context, promise.get(), "requestId",
                            JS_NewInt32(state.context, job->id), 0);
  job->opaque = opaque;
  if (opaque) job->load_blocker = state.lifecycle.block_load();
  job->script = script;
  if (script) job->script_url = request.url;
  job->context = state.context;
  job->resolve = callbacks[0];
  job->reject = callbacks[1];
  job->cancelled = request.cancelled;
  job->queued_request = std::move(request);
  state.network.fetches.push_back(std::move(job));
  start_queued_http(state);
  return promise.release();
}

JSValue js_native_image_load(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  if (!argc) return JS_ThrowTypeError(context, "image URL required");
  auto& state = *static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  auto url = resolve_web_url(state.base_url, js_string(context, argv[0]));
  if (!url) return JS_ThrowTypeError(context, "invalid HTTP(S) image URL");
  if (state.network.image_requests >= 64)
    return JS_ThrowInternalError(context, "page image request quota exceeded");
  ++state.network.image_requests;
  HttpRequest request;
  request.owner_cancelled = state.cancelled;
  request.url = *url;
  request.cookie_origin = state.base_url;
  request.referrer_source = state.base_url;
  request.referrer_policy = element_referrer_policy(
      state.referrer_policy, argc > 1 ? dom_node_arg(context, argv[1]) : nullptr);
  request.cancelled = std::make_shared<std::atomic<bool>>(false);
  return enqueue_http(state, std::move(request), false, true);
}

JSValue js_native_script_load(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  if (!argc) return JS_ThrowTypeError(context, "script URL required");
  auto& state = *static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  const auto source_url = js_string(context, argv[0]);
  auto url = is_data_script_url(source_url) ? std::optional<std::string>(source_url)
                                            : resolve_web_url(state.base_url, source_url);
  if (!url) return JS_ThrowTypeError(context, "invalid HTTP(S) script URL");
  HttpRequest request;
  request.owner_cancelled = state.cancelled;
  request.url = *url;
  request.cookie_origin = state.base_url;
  request.referrer_source = state.base_url;
  request.referrer_policy = element_referrer_policy(
      state.referrer_policy, argc > 1 ? dom_node_arg(context, argv[1]) : nullptr);
  request.max_body_bytes = kMaxScriptBytes;
  auto* node = argc > 1 ? dom_node_arg(context, argv[1]) : nullptr;
  const bool module = node && script_kind(attr(node, "type")) == ScriptKind::Module;
  const auto identity =
      module ? *url : classic_script_id(*url, node ? dom_node_id(&state, node) : -1);
  if (!module && node)
    state.classic_script_contexts.insert_or_assign(dom_node_id(&state, node), identity);
  state.modules->remember_script(identity, *url, {request.referrer_source, request.referrer_policy},
                                 module);
  request.timeout_ms = kScriptRequestTimeoutMs;
  request.cancelled = std::make_shared<std::atomic<bool>>(false);
  auto promise = enqueue_http(state, std::move(request), true);
  if (!JS_IsException(promise) && !state.network.fetches.empty())
    state.network.fetches.back()->script_url = identity;
  return promise;
}

JSValue js_native_script_execute(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  if (argc < 2) return JS_ThrowTypeError(context, "script source and URL required");
  auto& state = *static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  if (!state.scripting_allowed) return JS_UNDEFINED;
  const auto source = js_string(context, argv[0]);
  const auto source_url = js_string(context, argv[1]);
  if (source.size() > kMaxScriptBytes)
    return JS_ThrowInternalError(context, "script source exceeds the response-size limit");
  // Use the current turn's interrupt deadline; script insertion must not reset it.
  const bool module = argc > 2 && JS_ToBool(context, argv[2]) > 0;
  const JsValueOwner global(context, JS_GetGlobalObject(context));
  const JsValueOwner document(context, JS_GetPropertyStr(context, global.get(), "document"));
  auto* node = argc > 3 ? dom_node_arg(context, argv[3]) : nullptr;
  const auto context_id = node ? dom_node_id(&state, node) : -1;
  const auto captured = state.classic_script_contexts.find(context_id);
  const auto filename = module ? source_url
                        : captured != state.classic_script_contexts.end()
                            ? captured->second
                            : classic_script_id(source_url, context_id);
  const auto base = node && !attr(node, "src").empty()
                        ? source_url
                        : property_string(context, document.get(), "baseURI", state.base_url);
  state.modules->remember_script(
      filename, base, {"", element_referrer_policy(state.referrer_policy, node)}, module);
  struct ErrorContextScope {
    ScriptContext& state;
    std::string previous;
    ~ErrorContextScope() { state.error_context = std::move(previous); }
  } error_scope{state, std::move(state.error_context)};
  state.error_context = filename;
  auto result = module ? state.modules->evaluate(context, source, filename)
                       : JS_Eval(context, source.data(), source.size(), filename.c_str(),
                                 JS_EVAL_TYPE_GLOBAL);
  if (JS_IsException(result)) return result;
  if (module) {
    if (JS_PromiseState(context, result) == JS_PROMISE_PENDING)
      state.pending_modules.push_back({JS_DupValue(context, result), filename, false});
    return result;
  }
  JS_FreeValue(context, result);
  return JS_UNDEFINED;
}
JSValue js_native_script_kind(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  return JS_NewInt32(context,
                     static_cast<int>(script_kind(argc ? js_string(context, argv[0]) : "")));
}
}  // namespace webcli
