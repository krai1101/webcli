#include "runtime/dom/component_tree.hpp"

#include <lexbor/html/interfaces/template_element.h>

#include <unordered_set>

#include "runtime/dom/dom_bindings.hpp"
#include "runtime/dom/dom_helpers.hpp"
#include "runtime/engine/script_context.hpp"
namespace webcli {
JSValue js_dom_shadow_root(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  auto* node = argc ? dom_node_arg(context, argv[0]) : nullptr;
  if (!node || node->type != LXB_DOM_NODE_TYPE_ELEMENT)
    return JS_ThrowTypeError(context, "shadow host required");
  const auto tag = tag_name(node);
  static const std::unordered_set<std::string> hosts{
      "article", "aside", "blockquote", "body",   "div",  "footer", "h1", "h2",      "h3",
      "h4",      "h5",    "h6",         "header", "main", "nav",    "p",  "section", "span"};
  if (tag.find('-') == std::string::npos && !hosts.count(tag))
    return JS_ThrowTypeError(context, "this element cannot host a shadow root");
  auto& state = *static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  if (state.dom.shadow_roots.count(node))
    return JS_ThrowTypeError(context, "shadow root already attached");
  state.dom.allocations.reserve(0, 1);
  auto* fragment = lxb_dom_document_create_document_fragment(node->owner_document);
  if (!fragment) return JS_ThrowOutOfMemory(context);
  auto* root = lxb_dom_interface_node(fragment);
  const auto id = dom_node_id(&state, root);
  state.dom.shadow_hosts.emplace(root, node);
  try {
    state.dom.shadow_roots.emplace(node, root);
  } catch (...) {
    state.dom.shadow_hosts.erase(root);
    throw;
  }
  return JS_NewInt32(context, id);
}
JSValue js_dom_template_content(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  auto* node = argc ? dom_node_arg(context, argv[0]) : nullptr;
  if (!node || node->ns != LXB_NS_HTML || tag_name(node) != "template")
    return JS_ThrowTypeError(context, "HTML template required");
  auto& state = *static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  return JS_NewInt32(
      context,
      dom_node_id(&state, lxb_dom_interface_node(lxb_html_interface_template(node)->content)));
}
void prepare_composition(ScriptContext& state) {
  state.dom.assigned_slots.clear();
  state.dom.slot_children.clear();
  for (const auto& entry : state.dom.shadow_roots) {
    std::unordered_map<std::string, lxb_dom_node_t*> slots;
    std::vector<lxb_dom_node_t*> pending{entry.second};
    while (!pending.empty()) {
      auto* node = pending.back();
      pending.pop_back();
      if (tag_name(node) == "slot") {
        slots.emplace(attr(node, "name"), node);
        state.dom.slot_children.emplace(node, std::vector<lxb_dom_node_t*>{});
      }
      for (auto* child = node->last_child; child; child = child->prev) pending.push_back(child);
    }
    for (auto* child = entry.first->first_child; child; child = child->next) {
      if (child->type != LXB_DOM_NODE_TYPE_ELEMENT && child->type != LXB_DOM_NODE_TYPE_TEXT)
        continue;
      const auto found = slots.find(attr(child, "slot"));
      if (found != slots.end()) {
        state.dom.assigned_slots.emplace(child, found->second);
        state.dom.slot_children[found->second].push_back(child);
      }
    }
  }
}
std::vector<lxb_dom_node_t*> composed_children(ScriptContext& state, lxb_dom_node_t* node) {
  const auto root = state.dom.shadow_roots.find(node);
  if (root != state.dom.shadow_roots.end()) return {root->second};
  std::vector<lxb_dom_node_t*> result;
  const auto assigned = state.dom.slot_children.find(node);
  if (assigned != state.dom.slot_children.end()) result = assigned->second;
  if (result.empty())
    for (auto* child = node->first_child; child; child = child->next) result.push_back(child);
  return result;
}
lxb_dom_node_t* composed_parent(ScriptContext& state, lxb_dom_node_t* node) {
  if (const auto found = state.dom.shadow_hosts.find(node); found != state.dom.shadow_hosts.end())
    return found->second;
  if (const auto found = state.dom.assigned_slots.find(node);
      found != state.dom.assigned_slots.end())
    return found->second;
  return node ? node->parent : nullptr;
}
}  // namespace webcli
