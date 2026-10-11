#pragma once
#include <lexbor/dom/interfaces/node.h>
#include <quickjs.h>

#include <vector>
namespace webcli {
struct ScriptContext;
JSValue js_dom_shadow_root(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_dom_template_content(JSContext*, JSValueConst, int, JSValueConst*);
void prepare_composition(ScriptContext&);
std::vector<lxb_dom_node_t*> composed_children(ScriptContext&, lxb_dom_node_t*);
lxb_dom_node_t* composed_parent(ScriptContext&, lxb_dom_node_t*);
}  // namespace webcli
