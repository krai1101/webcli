#pragma once
#include <quickjs.h>

namespace webcli {
class JsRuntime;
struct ScriptContext;
void advance_document_lifecycle(JsRuntime&, ScriptContext&);
JSValue js_document_readiness(JSContext*, JSValueConst, int, JSValueConst*);
}  // namespace webcli
