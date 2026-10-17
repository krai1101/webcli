#pragma once
#include <lexbor/dom/interfaces/node.h>
#include <quickjs.h>

#include <string>
#include <utility>
namespace webcli {
struct ScriptContext;
lxb_dom_node_t* dom_node_arg(JSContext*, JSValueConst);
int dom_node_id(ScriptContext*, lxb_dom_node_t*);
JSValue js_dom_node_name(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_dom_text_get(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_dom_text_set(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_dom_attr_get(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_dom_attr_set(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_dom_attr_remove(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_dom_query(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_dom_create_element(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_dom_create_comment(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_dom_create_fragment(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_dom_clone(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_dom_attributes(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_dom_parent(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_dom_children(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_dom_append(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_dom_remove(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_dom_node_type(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_dom_create_text(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_dom_insert(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_dom_html(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_dom_selected_index(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_dom_textarea_value(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_dom_form_owner(JSContext*, JSValueConst, int, JSValueConst*);
lxb_dom_node_t* form_owner(ScriptContext&, lxb_dom_node_t*);
void reset_parser_form_owners(ScriptContext&, lxb_dom_node_t*);
std::string textarea_control_value(ScriptContext&, lxb_dom_node_t*);
std::pair<bool, std::string> selected_control_value(ScriptContext&, lxb_dom_node_t*);
void account_initial_dom(ScriptContext&);
}  // namespace webcli
