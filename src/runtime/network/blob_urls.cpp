#include "runtime/network/blob_urls.hpp"

#include "runtime/engine/js_handles.hpp"
#include "runtime/engine/script_context.hpp"

namespace webcli {
JSValue js_blob_url_register(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  if (argc < 2 || !JS_IsArrayBuffer(argv[1]))
    return JS_ThrowTypeError(context, "blob URL and ArrayBuffer required");
  auto& state = *static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  const auto url = js_string(context, argv[0]);
  std::size_t length = 0;
  const auto* bytes = JS_GetArrayBuffer(context, &length, argv[1]);
  if (JS_HasException(context)) return JS_EXCEPTION;
  if (url.rfind("blob:", 0) != 0) return JS_ThrowTypeError(context, "blob URL required");
  if (url.size() > 4096) return JS_ThrowRangeError(context, "blob URL exceeds 4096 bytes");
  if (state.network.blob_urls.count(url))
    return JS_ThrowTypeError(context, "blob URL already registered");
  if (state.network.blob_urls.size() >= kBlobUrlCount ||
      length > kBlobUrlBytes - state.network.blob_url_bytes)
    return JS_ThrowInternalError(context, "blob URL storage quota exceeded");
  state.network.blob_urls.emplace(
      url, std::string(bytes ? reinterpret_cast<const char*>(bytes) : "", length));
  state.network.blob_url_bytes += length;
  return JS_UNDEFINED;
}
JSValue js_blob_url_revoke(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  if (!argc) return JS_UNDEFINED;
  auto& state = *static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  const auto found = state.network.blob_urls.find(js_string(context, argv[0]));
  if (found != state.network.blob_urls.end()) {
    state.network.blob_url_bytes -= found->second.size();
    state.network.blob_urls.erase(found);
  }
  return JS_UNDEFINED;
}
}  // namespace webcli
