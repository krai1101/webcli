#include <lexbor/html/interfaces/template_element.h>

#include "runtime/dom/node_clone.hpp"
#include "runtime/dom/dom_bindings.hpp"
#include "runtime/dom/dom_helpers.hpp"
#include "runtime/engine/script_context.hpp"

namespace webcli {
NodeCopies clone_native_tree(ScriptContext& state, lxb_dom_node_t* node,
                           lxb_dom_document_t* target, bool deep, bool include_shadow) {
  std::vector<lxb_dom_node_t*> originals{node};
  if (deep) {
    for (std::size_t i = 0; i < originals.size(); ++i) {
      if (originals[i]->ns == LXB_NS_HTML && tag_name(originals[i]) == "template")
        originals.push_back(
            lxb_dom_interface_node(lxb_html_interface_template(originals[i])->content));
      if (include_shadow) {
        const auto shadow = state.dom.shadow_roots.find(originals[i]);
        if (shadow != state.dom.shadow_roots.end()) originals.push_back(shadow->second);
      }
      for (auto* child = originals[i]->first_child; child; child = child->next) {
        originals.push_back(child);
        if (originals.size() > DomAllocationBudget::max_nodes - state.dom.nodes.size())
          throw CliError("DOM_QUOTA", "DOM node quota exceeded");
      }
    }
  }
  if (!deep && node->ns == LXB_NS_HTML && tag_name(node) == "template")
    originals.push_back(lxb_dom_interface_node(lxb_html_interface_template(node)->content));
  if (originals.size() > DomAllocationBudget::max_nodes - state.dom.nodes.size())
    throw CliError("DOM_QUOTA", "DOM node quota exceeded");
  std::size_t clone_bytes = 0, clone_nodes = originals.size();
  for (auto* original : originals) {
    const auto textarea = state.dom.textarea_values.find(original);
    if (textarea != state.dom.textarea_values.end()) clone_bytes += textarea->second.size();
    if (original->type == LXB_DOM_NODE_TYPE_TEXT || original->type == LXB_DOM_NODE_TYPE_COMMENT)
      clone_bytes += lxb_dom_interface_character_data(original)->data.length;
    if (original->type == LXB_DOM_NODE_TYPE_ELEMENT)
      for (auto* attribute = lxb_dom_interface_element(original)->first_attr; attribute;
           attribute = attribute->next) {
        std::size_t length = 0;
        ++clone_nodes;
        lxb_dom_attr_value(attribute, &length);
        clone_bytes += length;
        lxb_dom_attr_qualified_name(attribute, &length);
        clone_bytes += length;
      }
  }
  state.dom.allocations.reserve(clone_bytes, clone_nodes);
  // Clone shallow nodes explicitly: Lexbor templates keep their content in a
  // separate fragment, outside the ordinary child list.
  std::unordered_map<lxb_dom_node_t*, lxb_dom_node_t*> cloned_nodes;
  std::unordered_map<lxb_dom_node_t*, lxb_dom_node_t*> template_contents;
  for (auto* original : originals) {
    lxb_dom_node_t* copy = nullptr;
    const auto content = template_contents.find(original);
    if (content != template_contents.end())
      copy = content->second;
    else {
      copy = lxb_dom_document_import_node(target, original, false);
      if (!copy) throw CliError("DOM_ALLOCATION", "could not clone DOM node");
      if (original->parent) {
        const auto parent = cloned_nodes.find(original->parent);
        if (parent != cloned_nodes.end() &&
            lxb_dom_node_append_child(parent->second, copy) != LXB_DOM_EXCEPTION_OK)
          throw CliError("DOM_ALLOCATION", "could not assemble cloned DOM tree");
      }
    }
    if (original->ns == LXB_NS_HTML && tag_name(original) == "template")
      template_contents.emplace(
          lxb_dom_interface_node(lxb_html_interface_template(original)->content),
          lxb_dom_interface_node(lxb_html_interface_template(copy)->content));
    cloned_nodes.emplace(original, copy);
  }
  return cloned_nodes;
}

JSValue js_dom_clone(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  auto* node = argc ? dom_node_arg(context, argv[0]) : nullptr;
  if (!node || node->type == LXB_DOM_NODE_TYPE_DOCUMENT)
    return JS_ThrowTypeError(context, "cloneNode requires a supported node");
  auto& state = *static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  const bool deep = argc > 1 && JS_ToBool(context, argv[1]) > 0;
  auto* owner = argc > 2 ? dom_node_arg(context, argv[2]) : nullptr;
  if (argc > 2 && (!owner || owner->type != LXB_DOM_NODE_TYPE_DOCUMENT))
    return JS_ThrowTypeError(context, "importNode requires a document");
  auto* target = owner ? lxb_dom_interface_document(owner) : node->owner_document;
  const auto copies = clone_native_tree(state, node, target, deep, false);
  for (const auto& [original, copy] : copies) {
    dom_node_id(&state, copy);
    const auto textarea = state.dom.textarea_values.find(original);
    if (textarea != state.dom.textarea_values.end())
      state.dom.textarea_values.emplace(copy, textarea->second);
    const auto selection = state.dom.selected_options.find(original);
    if (selection != state.dom.selected_options.end()) {
      const auto selected = copies.find(selection->second);
      state.dom.selected_options[copy] = selected == copies.end() ? nullptr : selected->second;
    }
  }
  return JS_NewInt32(context, dom_node_id(&state, copies.at(node)));
}
}  // namespace webcli
