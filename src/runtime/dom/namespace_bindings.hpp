#pragma once
#include <lexbor/dom/interfaces/node.h>
#include <quickjs.h>

#include <string>
namespace webcli {
std::string dom_namespace_uri(lxb_dom_node_t*);
JSValue js_dom_element_identity(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_dom_create_element_ns(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_dom_attribute_ns(JSContext*, JSValueConst, int, JSValueConst*);
}  // namespace webcli
