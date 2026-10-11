#include "runtime/network/web_bindings.hpp"

#include <curl/curl.h>
#include <uv.h>

#include <algorithm>
#include <cctype>
#include <vector>

#include "core/text.hpp"
#include "network/cookie_store.hpp"
#include "network/cors.hpp"
#include "network/http_client.hpp"
#include "runtime/dom/dom_helpers.hpp"
#include "runtime/engine/js_handles.hpp"
#include "runtime/engine/script_context.hpp"
#include "runtime/frames/frame_tree.hpp"
#include "runtime/network/resource_tasks.hpp"
namespace webcli {
JSValue js_native_cancel_fetch(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  int32_t id = 0;
  if (!argc) return JS_ThrowTypeError(context, "request id required");
  if (JS_ToInt32(context, &id, argv[0]) < 0) return JS_EXCEPTION;
  auto& state = *static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  for (const auto& job : state.network.fetches)
    if (job->id == id && job->cancelled) job->cancelled->store(true);
  return JS_UNDEFINED;
}
JSValue js_native_random_bytes(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  int64_t length = 0;
  if (!argc) return JS_ThrowTypeError(context, "random byte length required");
  if (JS_ToInt64(context, &length, argv[0]) < 0) return JS_EXCEPTION;
  if (length < 0 || length > 65536)
    return JS_ThrowRangeError(context, "random byte limit exceeded");
  std::vector<uint8_t> bytes(static_cast<std::size_t>(length));
  if (length && uv_random(nullptr, nullptr, bytes.data(), bytes.size(), 0, nullptr) != 0)
    return JS_ThrowInternalError(context, "secure random source unavailable");
  return JS_NewArrayBufferCopy(context, bytes.data(), bytes.size());
}
JSValue js_report_error(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  auto* state = static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  if (argc && state->errors->size() < kMaxScriptErrors) {
    const auto prefix =
        argc > 1 && js_string(context, argv[1]) == "warning" ? "console.warn: " : "console.error: ";
    state->errors->push_back(prefix + truncate(js_string(context, argv[0]), 1200));
  }
  return JS_UNDEFINED;
}
JSValue js_native_resolve(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  if (argc < 1) return JS_ThrowTypeError(context, "URL is required");
  auto* state = static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  const auto base = argc > 1 ? js_string(context, argv[0]) : state->base_url;
  auto resolved = resolve_web_url(base, js_string(context, argv[argc > 1 ? 1 : 0]));
  if (!resolved) return JS_ThrowTypeError(context, "invalid HTTP URL");
  return JS_NewString(context, resolved->c_str());
}
JSValue js_native_navigate(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  if (argc < 1) return JS_ThrowTypeError(context, "navigation URL is required");
  auto* state = static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  const auto base = argc > 1 ? js_string(context, argv[1]) : state->base_url;
  auto resolved = resolve_web_url(base, js_string(context, argv[0]));
  if (!resolved || (resolved->rfind("http://", 0) != 0 && resolved->rfind("https://", 0) != 0))
    return JS_ThrowTypeError(context, "only HTTP(S) navigation is supported");
  return JS_NewString(context, resolved->c_str());
}

JSValue js_native_fetch(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  auto* state = static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  try {
    if (!argc) return JS_ThrowTypeError(context, "fetch URL is required");
    if (state->network.fetches.size() >= kResourceQueueSize ||
        ++state->network.requests > kFetchRequestCount)
      return JS_ThrowInternalError(context, "page fetch quota exceeded");
    auto url = resolve_web_url(state->base_url, js_string(context, argv[0]));
    if (!url) return JS_ThrowTypeError(context, "invalid fetch URL");
    HttpRequest request;
    request.owner_cancelled = state->cancelled;
    request.url = *url;
    request.fetch_origin = state->opaque_origin ? "null" : state->security_origin;
    request.fetch_mode = "cors";
    request.cookie_origin = state->worker_channel ? state->security_origin : state->base_url;
    request.use_cookies = !state->opaque_origin;
    request.referrer_source = state->base_url;
    request.cancelled = std::make_shared<std::atomic<bool>>(false);
    if (argc > 1 && JS_IsObject(argv[1])) {
      const auto credentials = property_string(context, argv[1], "credentials", "same-origin");
      if (credentials == "omit")
        request.use_cookies = false;
      else if (credentials == "include") {
        request.use_cookies = true;
        request.cookie_origin.clear();
        request.fetch_credentials = true;
      } else if (credentials != "same-origin")
        return JS_ThrowTypeError(context, "invalid fetch credentials mode");
      const auto mode = property_string(context, argv[1], "mode", "cors");
      request.fetch_mode = mode;
      if (mode == "same-origin")
        request.allowed_origin = request.fetch_origin;
      else if (mode != "cors" && mode != "no-cors")
        return JS_ThrowTypeError(context, "invalid fetch mode");
      request.method = property_string(context, argv[1], "method", "GET");
      const auto referrer = property_string(context, argv[1], "referrer", "about:client");
      if (referrer.empty())
        request.referrer_source.clear();
      else if (referrer != "about:client") {
        const auto resolved = resolve_web_url(state->base_url, referrer);
        if (!resolved) return JS_ThrowTypeError(context, "invalid request referrer");
        if (same_origin(*resolved, state->base_url)) request.referrer_source = *resolved;
      }
      request.referrer_policy = property_string(context, argv[1], "referrerPolicy", "");
      if (request.referrer_policy.empty()) request.referrer_policy = state->referrer_policy;
      request.redirect_policy = property_string(context, argv[1], "redirect", "follow");
      if (request.redirect_policy != "follow" && request.redirect_policy != "manual" &&
          request.redirect_policy != "error")
        return JS_ThrowTypeError(context, "invalid redirect policy");
      std::transform(request.method.begin(), request.method.end(), request.method.begin(),
                     [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
      if (request.method.empty() || request.method == "CONNECT" || request.method == "TRACE" ||
          request.method == "TRACK" ||
          !std::all_of(request.method.begin(), request.method.end(), [](unsigned char c) {
            return std::isalnum(c) || std::string("!#$%&'*+-.^_`|~").find(c) != std::string::npos;
          }))
        return JS_ThrowTypeError(context, "unsupported HTTP method");
      const JsValueOwner body(context, JS_GetPropertyStr(context, argv[1], "body"));
      if (JS_IsException(body.get())) return JS_EXCEPTION;
      if (JS_IsArrayBuffer(body.get())) {
        std::size_t size = 0;
        auto* bytes = JS_GetArrayBuffer(context, &size, body.get());
        if (JS_HasException(context)) return JS_EXCEPTION;
        request.body.assign(bytes ? reinterpret_cast<const char*>(bytes) : "", size);
      } else if (!JS_IsNull(body.get()) && !JS_IsUndefined(body.get()))
        request.body = js_string(context, body.get());
      if (request.body.size() > kFetchBodyBytes)
        return JS_ThrowTypeError(context, "fetch body exceeds 1 MiB");
      JsValueOwner headers(context, JS_GetPropertyStr(context, argv[1], "headers"));
      JSPropertyEnum* props = nullptr;
      uint32_t count = 0;
      if (JS_IsException(headers.get())) return JS_EXCEPTION;
      if (JS_IsObject(headers.get())) {
        if (JS_GetOwnPropertyNames(context, &props, &count, headers.get(),
                                   JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) < 0)
          return JS_EXCEPTION;
        struct PropertyNames {
          JSContext* context;
          JSPropertyEnum* names;
          uint32_t count;
          ~PropertyNames() {
            for (uint32_t i = 0; i < count; ++i) JS_FreeAtom(context, names[i].atom);
            js_free(context, names);
          }
        } names{context, props, count};
        if (count > 128) return JS_ThrowTypeError(context, "fetch exceeds 128 headers");
        std::size_t bytes = 0;
        for (uint32_t i = 0; i < count; ++i) {
          JsValueOwner key(context, JS_AtomToString(context, props[i].atom));
          JsValueOwner value(context, JS_GetProperty(context, headers.get(), props[i].atom));
          if (JS_IsException(key.get()) || JS_IsException(value.get())) return JS_EXCEPTION;
          auto name = js_string(context, key.get());
          auto text = js_string(context, value.get());
          bytes += name.size() + text.size();
          if (bytes > kFetchHeaderBytes)
            return JS_ThrowTypeError(context, "fetch headers exceed 64 KiB");
          request.headers.emplace(std::move(name), std::move(text));
        }
      }
    }
    prepare_fetch(request);
    return enqueue_http(*state, std::move(request));
  } catch (const std::exception& error) {
    return JS_ThrowInternalError(context, "%s", error.what());
  }
}
JSValue js_native_cookie(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  auto* state = static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  if (state->opaque_origin)
    return frame_security_error(context, "opaque frame origin has no cookies");
  if (!argc) return JS_ThrowTypeError(context, "cookie URL required");
  auto url = js_string(context, argv[0]);
  if (url == "about:blank" || url == "about:srcdoc") url = state->base_url;
  if (!same_origin(url, state->base_url))
    return JS_ThrowTypeError(context, "cookie URL must be same-origin");
  auto store = profile_cookies();
  if (argc > 1) {
    store->set_document_cookie(url, js_string(context, argv[1]));
    return JS_UNDEFINED;
  }
  const auto value = store->document_cookie(url);
  return JS_NewStringLen(context, value.data(), value.size());
}

JSValue js_native_storage(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  auto* state = static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  if (state->opaque_origin)
    return frame_security_error(context, "opaque frame origin has no storage");
  try {
    if (argc < 2) return JS_ThrowTypeError(context, "storage operation required");
    bool persistent = JS_ToBool(context, argv[0]) != 0;
    const auto op = js_string(context, argv[1]);
    const auto origin = url_origin(state->base_url);
    auto key = argc > 2 ? js_string(context, argv[2]) : "";
    const auto incoming = op == "set" && argc > 3 ? js_string(context, argv[3]) : "";
    auto storage_lock = state->storage->lock();
    auto& values = state->storage->data(origin, persistent);
    if (op == "get") {
      auto it = values.find(key);
      return it == values.end() ? JS_NULL
                                : JS_NewStringLen(context, it->second.data(), it->second.size());
    }
    if (op == "keys") {
      JSValue result = JS_NewArray(context);
      uint32_t index = 0;
      for (const auto& pair : values)
        JS_SetPropertyUint32(context, result, index++,
                             JS_NewStringLen(context, pair.first.data(), pair.first.size()));
      return result;
    }
    auto next = values;
    if (op == "set" && argc > 3)
      next[key] = incoming;
    else if (op == "remove")
      next.erase(key);
    else if (op == "clear")
      next.clear();
    else
      return JS_ThrowTypeError(context, "invalid storage operation");
    std::size_t bytes = 0;
    for (const auto& pair : next) bytes += pair.first.size() + pair.second.size();
    if (bytes > 1024 * 1024 || next.size() > 1024)
      return JS_ThrowInternalError(context, "storage quota exceeded (1 MiB / 1024 keys)");
    values.swap(next);
    try {
      if (persistent) state->storage->save(origin);
    } catch (...) {
      values.swap(next);
      throw;
    }
    return JS_UNDEFINED;
  } catch (const std::exception& error) {
    return JS_ThrowInternalError(context, "%s", error.what());
  }
}

}  // namespace webcli
