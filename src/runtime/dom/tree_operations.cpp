#include <algorithm>
#include <cstring>

#include "runtime/dom/dom_bindings.hpp"
#include "runtime/dom/dom_helpers.hpp"
#include "runtime/dom/node_adoption.hpp"
#include "runtime/engine/js_handles.hpp"
#include "runtime/engine/script_context.hpp"

namespace webcli {
JSValue js_dom_create_element(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  if (argc < 1) return JS_ThrowTypeError(context, "tag name is required");
  auto* state = static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  state->dom.allocations.reserve(0, 1);
  const JsCString name_owner(context, argv[0]);
  const char* name = name_owner.get();
  state->dom.allocations.reserve(std::strlen(name));
  std::string normalized = name;
  std::transform(
      normalized.begin(), normalized.end(), normalized.begin(),
      [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
  if (normalized == "template") state->dom.allocations.reserve(0, 1);
  auto* element = lxb_dom_document_create_element(&state->dom.document->dom_document,
                                                  reinterpret_cast<const lxb_char_t*>(name),
                                                  std::strlen(name), nullptr);

  if (!element) return JS_ThrowInternalError(context, "could not create DOM element");
  return JS_NewInt32(context, dom_node_id(state, lxb_dom_interface_node(element)));
}

JSValue js_dom_create_comment(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  auto* state = static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  auto* owner = argc > 1 ? dom_node_arg(context, argv[1])
                         : lxb_dom_interface_node(&state->dom.document->dom_document);
  if (!owner || owner->type != LXB_DOM_NODE_TYPE_DOCUMENT)
    return JS_ThrowTypeError(context, "document required");
  const auto text = argc ? js_string(context, argv[0]) : "";
  state->dom.allocations.reserve(text.size(), 1);
  auto* comment = lxb_dom_document_create_comment(lxb_dom_interface_document(owner),
                                                  reinterpret_cast<const lxb_char_t*>(text.data()),
                                                  text.size());
  if (!comment) return JS_ThrowInternalError(context, "could not create DOM comment");
  return JS_NewInt32(context, dom_node_id(state, lxb_dom_interface_node(comment)));
}

JSValue js_dom_create_fragment(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  auto* state = static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  auto* owner = argc ? dom_node_arg(context, argv[0])
                     : lxb_dom_interface_node(&state->dom.document->dom_document);
  if (!owner || owner->type != LXB_DOM_NODE_TYPE_DOCUMENT)
    return JS_ThrowTypeError(context, "document required");
  state->dom.allocations.reserve(0, 1);
  auto* fragment = lxb_dom_document_create_document_fragment(lxb_dom_interface_document(owner));
  if (!fragment) return JS_ThrowInternalError(context, "could not create document fragment");
  return JS_NewInt32(context, dom_node_id(state, lxb_dom_interface_node(fragment)));
}

JSValue js_dom_parent(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  if (argc < 1) return JS_ThrowTypeError(context, "node id is required");
  auto* state = static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  auto* node = dom_node_arg(context, argv[0]);
  return JS_NewInt32(context, node ? dom_node_id(state, lxb_dom_node_parent(node)) : -1);
}

JSValue js_dom_children(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  if (argc < 1) return JS_ThrowTypeError(context, "node id is required");
  auto* state = static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  auto* node = dom_node_arg(context, argv[0]);
  JSValue output = JS_NewArray(context);
  uint32_t i = 0;
  for (auto* child = node ? lxb_dom_node_first_child(node) : nullptr; child;
       child = lxb_dom_node_next(child))
    JS_SetPropertyUint32(context, output, i++, JS_NewInt32(context, dom_node_id(state, child)));
  return output;
}

// Shadow roots are detached native fragments but still participate in ancestor checks.
static bool insertion_cycle(JSContext* context, lxb_dom_node_t* parent, lxb_dom_node_t* child) {
  const auto& state = *static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  for (auto* ancestor = parent; ancestor;) {
    if (ancestor == child) return true;
    const auto host = state.dom.shadow_hosts.find(ancestor);
    ancestor = host == state.dom.shadow_hosts.end() ? ancestor->parent : host->second;
  }
  return false;
}

JSValue js_dom_append(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  if (argc < 2) return JS_ThrowTypeError(context, "parent and child ids are required");
  auto* parent = dom_node_arg(context, argv[0]);
  auto* child = dom_node_arg(context, argv[1]);
  if (!parent || !child) return JS_ThrowTypeError(context, "invalid DOM node");
  if (insertion_cycle(context, parent, child))
    return JS_ThrowTypeError(context, "cannot insert a shadow-including ancestor");
  if (lxb_dom_node_pre_insert_validity(parent, child, nullptr) != LXB_DOM_EXCEPTION_OK)
    return JS_ThrowTypeError(context, "cannot append this DOM node");
  auto& state = *static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  child = adopt_native_tree(state, child, parent->owner_document);
  reset_parser_form_owners(state, child);
  if (lxb_dom_node_append_child(parent, child) != LXB_DOM_EXCEPTION_OK)
    return JS_ThrowTypeError(context, "cannot append this DOM node");
  return JS_DupValue(context, argv[1]);
}

JSValue js_dom_remove(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  if (argc < 1) return JS_ThrowTypeError(context, "node id is required");
  auto* node = dom_node_arg(context, argv[0]);
  if (!node) return JS_ThrowTypeError(context, "invalid DOM node");
  reset_parser_form_owners(*static_cast<ScriptContext*>(JS_GetContextOpaque(context)), node);
  lxb_dom_node_remove(node);
  return JS_UNDEFINED;
}

JSValue js_dom_insert(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  if (argc < 3) return JS_ThrowTypeError(context, "parent, child and reference required");
  auto* parent = dom_node_arg(context, argv[0]);
  auto* child = dom_node_arg(context, argv[1]);
  auto* before = dom_node_arg(context, argv[2]);
  if (!parent || !child || !before || lxb_dom_node_parent(before) != parent)
    return JS_ThrowTypeError(context, "invalid insertBefore target");
  if (insertion_cycle(context, parent, child))
    return JS_ThrowTypeError(context, "cannot insert a shadow-including ancestor");
  if (lxb_dom_node_pre_insert_validity(parent, child, before) != LXB_DOM_EXCEPTION_OK)
    return JS_ThrowTypeError(context, "invalid insertBefore operation");
  auto& state = *static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  child = adopt_native_tree(state, child, parent->owner_document);
  reset_parser_form_owners(state, child);
  if (lxb_dom_node_insert_before_spec(parent, child, before) != LXB_DOM_EXCEPTION_OK)
    return JS_ThrowTypeError(context, "invalid insertBefore operation");
  return JS_UNDEFINED;
}

}  // namespace webcli
