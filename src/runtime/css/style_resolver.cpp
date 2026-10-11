#include "runtime/css/style_resolver.hpp"

#include "runtime/dom/component_tree.hpp"
#include "runtime/dom/dom_bindings.hpp"
#include "runtime/dom/dom_helpers.hpp"
#include "runtime/engine/js_handles.hpp"
#include "runtime/engine/script_context.hpp"

namespace webcli {
JSValue js_dom_revision(JSContext* context, JSValueConst, int, JSValueConst*) {
  return JS_NewInt64(
      context, static_cast<ScriptContext*>(JS_GetContextOpaque(context))->css.style_generation);
}
const VisibilityValues& resolve_visibility(ScriptContext& state) {
  if (state.css.style_resolved_generation == state.css.style_generation)
    return state.css.visibility_values;
  prepare_composition(state);
  VisibilityValues values;
  bool truncated = false;
  std::vector<lxb_dom_node_t*> roots{lxb_dom_interface_node(&state.dom.document->dom_document)};
  for (const auto& entry : state.dom.shadow_roots) roots.push_back(entry.second);
  for (auto* root : roots) {
    std::vector<lxb_dom_node_t*> nodes, pending{root};
    std::vector<StyleSource> sources;
    while (!pending.empty()) {
      auto* node = pending.back();
      pending.pop_back();
      nodes.push_back(node);
      const auto tag = tag_name(node);
      if (tag == "style" && !state.css.disabled_stylesheets.count(node)) {
        const auto found = state.css.inline_stylesheets.find(node);
        const auto original = node_text(node);
        sources.push_back(
            {found != state.css.inline_stylesheets.end() && found->second.original == original
                 ? found->second.source
                 : original,
             attr(node, "media")});
      } else {
        const auto found = state.css.stylesheets.find(node);
        if (found != state.css.stylesheets.end() && found->second.source &&
            !state.css.disabled_stylesheets.count(node) &&
            found->second.href == attr(node, "href") && !has_attr(node, "disabled")) {
          const auto override = state.css.inline_stylesheets.find(node);
          sources.push_back({override != state.css.inline_stylesheets.end() &&
                                     override->second.original == attr(node, "href")
                                 ? override->second.source
                                 : *found->second.source,
                             attr(node, "media")});
        }
      }
      for (auto* child = node->last_child; child; child = child->prev) pending.push_back(child);
    }
    const auto adopted = state.css.adopted_stylesheets.find(root);
    if (adopted != state.css.adopted_stylesheets.end()) sources.push_back({adopted->second, ""});
    auto scoped = stylesheet_visibility(root, nodes, sources, truncated);
    values.insert(scoped.begin(), scoped.end());
    // Closed popovers have a user-agent display:none rule regardless of author CSS.
    for (auto* node : nodes)
      if (has_attr(node, "popover") && !state.dom.open_popovers.count(node))
        values[node].display = "none";
  }
  state.css.visibility_values = std::move(values);
  state.css.style_truncated = truncated;
  state.css.style_resolved_generation = state.css.style_generation;
  return state.css.visibility_values;
}
JSValue js_popover_state(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  auto* node = argc ? dom_node_arg(context, argv[0]) : nullptr;
  if (!node) return JS_ThrowTypeError(context, "popover requires an element");
  auto& state = *static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  if (argc > 1) {
    if (JS_ToBool(context, argv[1])) state.dom.open_popovers.insert(node);
    else state.dom.open_popovers.erase(node);
    ++state.css.style_generation;
  }
  return JS_NewBool(context, state.dom.open_popovers.count(node) != 0);
}
JSValue js_computed_visibility(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  auto* node = argc ? dom_node_arg(context, argv[0]) : nullptr;
  if (!node) return JS_ThrowTypeError(context, "computed style requires a node");
  auto& state = *static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  const auto& values = resolve_visibility(state);
  if (argc > 1 && JS_ToBool(context, argv[1])) {
    // Resolve inherited visibility over the same composed parents used by
    // observation, including slot and shadow-host inheritance.
    for (auto* ancestor = node; ancestor; ancestor = composed_parent(state, ancestor)) {
      const auto match = values.find(ancestor);
      if (match == values.end()) continue;
      const auto& visibility = match->second.visibility;
      if (visibility == "initial") return JS_NewString(context, "visible");
      if (visibility == "visible" || visibility == "hidden" || visibility == "collapse")
        return JS_NewString(context, visibility.c_str());
    }
    return JS_NewString(context, "visible");
  }
  const auto found = values.find(node);
  // A null result asks the JS interface to apply its default or inheritance.
  if (found == values.end()) return JS_NULL;
  const int property = argc > 1 ? JS_ToBool(context, argv[1]) : 0;
  const auto& value = property ? found->second.visibility : found->second.display;
  return value.empty() ? JS_NULL : JS_NewStringLen(context, value.data(), value.size());
}
JSValue js_style_invalidate(JSContext* context, JSValueConst, int, JSValueConst*) {
  ++static_cast<ScriptContext*>(JS_GetContextOpaque(context))->css.style_generation;
  return JS_UNDEFINED;
}
JSValue js_adopted_stylesheet(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  auto* root = argc ? dom_node_arg(context, argv[0]) : nullptr;
  if (!root || (root->type != LXB_DOM_NODE_TYPE_DOCUMENT &&
                root->type != LXB_DOM_NODE_TYPE_DOCUMENT_FRAGMENT))
    return JS_ThrowTypeError(context, "adopted stylesheet requires a document or shadow root");
  auto& state = *static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  const auto source = argc > 1 ? js_string(context, argv[1]) : "";
  const auto found = state.css.adopted_stylesheets.find(root);
  const auto previous = found == state.css.adopted_stylesheets.end() ? 0 : found->second.size();
  if (source.size() > kVisibilityStylesheetBytes - (state.css.adopted_stylesheet_bytes - previous))
    return JS_ThrowInternalError(context, "adopted stylesheet storage exceeds 2 MiB");
  state.css.adopted_stylesheets.insert_or_assign(root, source);
  state.css.adopted_stylesheet_bytes =
      state.css.adopted_stylesheet_bytes - previous + source.size();
  ++state.css.style_generation;
  return JS_UNDEFINED;
}
}  // namespace webcli
