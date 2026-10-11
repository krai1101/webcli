#include "runtime/dom/node_adoption.hpp"

#include "runtime/dom/dom_bindings.hpp"
#include "runtime/dom/node_clone.hpp"
#include "runtime/engine/script_context.hpp"

namespace webcli {
namespace {
lxb_dom_node_t* remap(lxb_dom_node_t* node, const NodeCopies& copies) {
  const auto found = copies.find(node);
  return found == copies.end() ? node : found->second;
}
template <typename Map>
Map remap_keys(const Map& source, const NodeCopies& copies) {
  Map result;
  result.reserve(source.size());
  for (const auto& [key, value] : source) result.emplace(remap(key, copies), value);
  return result;
}
template <typename Map>
Map remap_links(const Map& source, const NodeCopies& copies) {
  auto result = remap_keys(source, copies);
  for (auto& [key, value] : result) value = remap(value, copies);
  return result;
}
template <typename Set>
Set remap_set(const Set& source, const NodeCopies& copies) {
  Set result;
  result.reserve(source.size());
  for (auto* node : source) result.emplace(remap(node, copies));
  return result;
}
}  // namespace

lxb_dom_node_t* adopt_native_tree(ScriptContext& state, lxb_dom_node_t* node,
                                lxb_dom_document_t* target) {
  if (node->owner_document == target) return node;
  // The active Lexbor parser keeps direct pointers in its insertion stack.
  // Migrating those pointers would split parser state from the exposed tree.
  if (state.parser_write && node->owner_document == &state.dom.document->dom_document)
    throw CliError("DOM_ADOPTION", "cannot migrate live-document nodes while its parser is active");
  // Lexbor allocates node structures, strings and interned names in document
  // arenas. Changing owner_document alone would leave dangling allocations
  // when a parsed document is collected. Migrate storage, retaining wrapper IDs.
  const auto copies = clone_native_tree(state, node, target, true, true);
  auto node_ids = state.dom.node_ids;
  auto nodes = state.dom.nodes;
  for (const auto& [original, copy] : copies) {
    const auto found = node_ids.find(original);
    if (found == node_ids.end()) continue;
    const auto id = found->second;
    node_ids.erase(found);
    node_ids.emplace(copy, id);
    nodes[static_cast<std::size_t>(id)] = copy;
  }
  // Stage all throwing allocations before detaching the source or rebinding
  // IDs. Native allocation accounting remains conservative until arena teardown.
  auto selections = remap_links(state.dom.selected_options, copies);
  auto textareas = remap_keys(state.dom.textarea_values, copies);
  auto roots = remap_links(state.dom.shadow_roots, copies);
  auto hosts = remap_links(state.dom.shadow_hosts, copies);
  auto popovers = remap_set(state.dom.open_popovers, copies);
  auto stylesheets = remap_keys(state.css.stylesheets, copies);
  auto inline_styles = remap_keys(state.css.inline_stylesheets, copies);
  auto adopted_styles = remap_keys(state.css.adopted_stylesheets, copies);
  auto disabled_styles = remap_set(state.css.disabled_stylesheets, copies);
  lxb_dom_node_remove(node);
  state.dom.node_ids.swap(node_ids);
  state.dom.nodes.swap(nodes);
  state.dom.selected_options.swap(selections);
  state.dom.textarea_values.swap(textareas);
  state.dom.shadow_roots.swap(roots);
  state.dom.shadow_hosts.swap(hosts);
  state.dom.open_popovers.swap(popovers);
  state.dom.assigned_slots.clear();
  state.dom.slot_children.clear();
  state.css.stylesheets.swap(stylesheets);
  state.css.inline_stylesheets.swap(inline_styles);
  state.css.adopted_stylesheets.swap(adopted_styles);
  state.css.disabled_stylesheets.swap(disabled_styles);
  state.css.visibility_values.clear();
  ++state.css.style_generation;
  return copies.at(node);
}

JSValue js_dom_adopt(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  auto* owner = argc > 0 ? dom_node_arg(context, argv[0]) : nullptr;
  auto* node = argc > 1 ? dom_node_arg(context, argv[1]) : nullptr;
  auto& state = *static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  if (!owner || owner->type != LXB_DOM_NODE_TYPE_DOCUMENT || !node ||
      node->type == LXB_DOM_NODE_TYPE_DOCUMENT || state.dom.shadow_hosts.count(node))
    return JS_ThrowTypeError(context, "invalid adoption target");
  lxb_dom_node_t* adopted;
  try {
    adopted = adopt_native_tree(state, node, lxb_dom_interface_document(owner));
  } catch (const CliError& error) {
    if (error.code == "DOM_ADOPTION")
      return JS_ThrowDOMException(context, "NotSupportedError", "%s", error.what());
    throw;
  }
  lxb_dom_node_remove(adopted);
  return JS_DupValue(context, argv[1]);
}

JSValue js_dom_validate_insert(JSContext* context, JSValueConst, int argc,
                               JSValueConst* argv) {
  auto* parent = argc > 0 ? dom_node_arg(context, argv[0]) : nullptr;
  auto* child = argc > 1 ? dom_node_arg(context, argv[1]) : nullptr;
  auto* before = argc > 2 ? dom_node_arg(context, argv[2]) : nullptr;
  if (!parent || !child) return JS_ThrowTypeError(context, "invalid insertion target");
  return JS_NewBool(context,
      lxb_dom_node_pre_insert_validity(parent, child, before) == LXB_DOM_EXCEPTION_OK);
}
}  // namespace webcli
