#include "runtime/url/url_bindings.hpp"

#include "core/url/url.hpp"
#include "runtime/engine/js_handles.hpp"

namespace webcli {
JSValue js_url_parse(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  if (!argc) return JS_ThrowTypeError(context, "URL required");
  try {
    std::optional<std::string> base;
    if (argc > 1 && !JS_IsUndefined(argv[1])) base = js_string(context, argv[1]);
    ParsedUrl url;
    if (!url.parse(js_string(context, argv[0]), base))
      return JS_ThrowTypeError(context, "Invalid URL or base");
    if (argc > 3 && !url.assign(js_string(context, argv[2]), js_string(context, argv[3])))
      return JS_ThrowTypeError(context, "Invalid URL");
    JsValueOwner result(context, JS_NewObject(context));
    if (JS_IsException(result.get())) return JS_EXCEPTION;
    for (const auto& field : url.parts()) {
      auto value = JS_NewStringLen(context, field.second.data(), field.second.size());
      if (JS_IsException(value)) return JS_EXCEPTION;
      if (JS_SetPropertyStr(context, result.get(), field.first, value) < 0) return JS_EXCEPTION;
    }
    return result.release();
  } catch (const CliError& error) {
    if (error.code == "URL_TOO_LARGE") return JS_ThrowRangeError(context, "%s", error.what());
    throw;
  }
}
}  // namespace webcli
