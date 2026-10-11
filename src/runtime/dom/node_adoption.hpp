#pragma once
#include <lexbor/dom/interfaces/document.h>
#include <quickjs.h>

namespace webcli {
struct ScriptContext;
lxb_dom_node_t* adopt_native_tree(ScriptContext&, lxb_dom_node_t*, lxb_dom_document_t*);
JSValue js_dom_adopt(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_dom_validate_insert(JSContext*, JSValueConst, int, JSValueConst*);
}  // namespace webcli
