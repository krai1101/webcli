#pragma once
#include <lexbor/dom/interfaces/node.h>
#include <quickjs.h>

#include <string>

namespace webcli {
struct ScriptContext;
void process_referrer_meta(ScriptContext&, lxb_dom_node_t*);
std::string element_referrer_policy(const std::string&, lxb_dom_node_t*);
JSValue js_process_referrer_meta(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_document_referrer(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_queue_navigation(JSContext*, JSValueConst, int, JSValueConst*);
}  // namespace webcli
