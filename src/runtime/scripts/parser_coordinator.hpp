#pragma once

#include <quickjs.h>

#include <string_view>

namespace webcli {
class JsRuntime;
struct ScriptContext;
struct Page;
void parse_live_document(JsRuntime&, ScriptContext&, Page&, std::string_view);
JSValue js_document_write(JSContext*, JSValueConst, int, JSValueConst*);
}  // namespace webcli
