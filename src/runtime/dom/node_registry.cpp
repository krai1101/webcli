#include <lexbor/dom/interfaces/document_type.h>

#include <cstring>

#include "runtime/dom/dom_bindings.hpp"
#include "runtime/dom/dom_helpers.hpp"
#include "runtime/dom/tree_usage.hpp"
#include "runtime/engine/js_handles.hpp"
#include "runtime/engine/script_context.hpp"

namespace webcli {
void account_initial_dom(ScriptContext& state) {
  const auto usage = measure_tree(lxb_dom_interface_node(&state.dom.document->dom_document));
  state.dom.allocations.reserve(usage.bytes, usage.nodes);
}

int dom_node_id(ScriptContext* state, lxb_dom_node_t* node) {
  if (!node) return -1;
  const auto found = state->dom.node_ids.find(node);
  if (found != state->dom.node_ids.end()) return found->second;
  if (!state->dom.free_node_ids.empty()) {
    const auto id = state->dom.free_node_ids.back();
    state->dom.node_ids.emplace(node, id);
    state->dom.free_node_ids.pop_back();
    state->dom.nodes[static_cast<std::size_t>(id)] = node;
    return id;
  }
  if (state->dom.nodes.size() >= DomAllocationBudget::max_nodes)
    throw CliError("DOM_QUOTA", "DOM node registry quota exceeded");
  const auto id = static_cast<int>(state->dom.nodes.size());
  state->dom.nodes.push_back(node);
  try {
    state->dom.node_ids.emplace(node, id);
  } catch (...) {
    state->dom.nodes.pop_back();
    throw;
  }
  return static_cast<int>(state->dom.nodes.size() - 1);
}

lxb_dom_node_t* dom_node_arg(JSContext* context, JSValueConst value) {
  auto* state = static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  int32_t id = -1;
  if (JS_ToInt32(context, &id, value) < 0 || id < 0 ||
      static_cast<std::size_t>(id) >= state->dom.nodes.size())
    return nullptr;
  return state->dom.nodes[static_cast<std::size_t>(id)];
}

JSValue js_dom_node_name(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  if (argc < 1) return JS_ThrowTypeError(context, "node id is required");
  auto* node = dom_node_arg(context, argv[0]);
  if (!node) return JS_ThrowTypeError(context, "invalid DOM node");
  if (node->type == LXB_DOM_NODE_TYPE_DOCUMENT_TYPE) {
    std::size_t length = 0;
    const auto* name = lxb_dom_document_type_name(lxb_dom_interface_document_type(node), &length);
    return name ? JS_NewStringLen(context, reinterpret_cast<const char*>(name), length)
                : JS_NewString(context, "");
  }
  const auto name = tag_name(node);
  return JS_NewStringLen(context, name.data(), name.size());
}

JSValue js_dom_text_get(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  if (argc < 1) return JS_ThrowTypeError(context, "node id is required");
  auto* node = dom_node_arg(context, argv[0]);
  if (!node) return JS_ThrowTypeError(context, "invalid DOM node");
  size_t length = 0;
  const auto* text = lxb_dom_node_text_content(node, &length);
  JSValue result = text ? JS_NewStringLen(context, reinterpret_cast<const char*>(text), length)
                        : JS_NewString(context, "");
  if (text) lxb_dom_document_destroy_text(node->owner_document, const_cast<lxb_char_t*>(text));
  return result;
}

JSValue js_dom_text_set(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  if (argc < 2) return JS_ThrowTypeError(context, "node id and text are required");
  auto* node = dom_node_arg(context, argv[0]);
  if (!node) return JS_ThrowTypeError(context, "invalid DOM node");
  const JsCString text_owner(context, argv[1]);
  const char* text = text_owner.get();
  auto* state = static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  state->dom.allocations.reserve(std::strlen(text),
                                 (node->type == LXB_DOM_NODE_TYPE_ELEMENT ||
                                  node->type == LXB_DOM_NODE_TYPE_DOCUMENT_FRAGMENT) &&
                                         *text
                                     ? 1
                                     : 0);
  // Detached nodes remain owned by the document: wrappers must never dangle.
  if (node->type == LXB_DOM_NODE_TYPE_ELEMENT || node->type == LXB_DOM_NODE_TYPE_DOCUMENT_FRAGMENT)
    while (auto* child = lxb_dom_node_first_child(node)) {
      reset_parser_form_owners(*state, child);
      lxb_dom_node_remove(child);
    }
  if (node->type == LXB_DOM_NODE_TYPE_TEXT || node->type == LXB_DOM_NODE_TYPE_COMMENT ||
      node->type == LXB_DOM_NODE_TYPE_PROCESSING_INSTRUCTION) {
    auto& data = lxb_dom_interface_character_data(node)->data;
    const auto length = std::strlen(text);
    // Lexbor 3.0.1's character-data replacement grows only when capacity <
    // length, then writes a terminator at length. Equality also needs growth.
    if (data.data && lexbor_str_size(&data) <= length &&
        !lexbor_str_realloc(&data, node->owner_document->text, length + 1))
      return JS_ThrowOutOfMemory(context);
  }
  const auto status = lxb_dom_node_text_content_set(node, reinterpret_cast<const lxb_char_t*>(text),
                                                    std::strlen(text));

  if (status != LXB_STATUS_OK) return JS_ThrowInternalError(context, "could not update DOM text");
  return JS_UNDEFINED;
}

JSValue js_dom_node_type(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  auto* node = argc ? dom_node_arg(context, argv[0]) : nullptr;
  return JS_NewInt32(context, node ? static_cast<int>(node->type) : 0);
}
JSValue js_dom_create_text(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  auto* state = static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  auto* owner = argc > 1 ? dom_node_arg(context, argv[1])
                         : lxb_dom_interface_node(&state->dom.document->dom_document);
  if (!owner || owner->type != LXB_DOM_NODE_TYPE_DOCUMENT)
    return JS_ThrowTypeError(context, "document required");
  if (!argc) return JS_ThrowTypeError(context, "text required");
  const JsCString text_owner(context, argv[0]);
  const char* text = text_owner.get();
  const auto size = std::strlen(text);
  state->dom.allocations.reserve(size, 1);
  auto* node = lxb_dom_document_create_text_node(lxb_dom_interface_document(owner),
                                                 reinterpret_cast<const lxb_char_t*>(text), size);
  if (!node) return JS_ThrowOutOfMemory(context);
  return JS_NewInt32(context, dom_node_id(state, lxb_dom_interface_node(node)));
}

}  // namespace webcli
