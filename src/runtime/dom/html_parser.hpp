#pragma once
#include <lexbor/dom/interfaces/node.h>
#include <quickjs.h>

#include <cstddef>
namespace webcli {
struct ScriptContext;
void release_attribute_bytes(ScriptContext&, lxb_dom_node_t*, std::size_t);
JSValue js_parse_html_document(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_node_owner_document(JSContext*, JSValueConst, int, JSValueConst*);
}  // namespace webcli
