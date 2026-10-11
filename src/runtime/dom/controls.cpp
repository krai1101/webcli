#include <lexbor/html/interfaces/template_element.h>

#include <algorithm>
#include <cstring>
#include <unordered_set>

#include "core/text.hpp"
#include "runtime/dom/dom_bindings.hpp"
#include "runtime/dom/dom_helpers.hpp"
#include "runtime/dom/namespace_bindings.hpp"
#include "runtime/dom/scoped_selector.hpp"
#include "runtime/dom/tree_usage.hpp"
#include "runtime/engine/js_handles.hpp"
#include "runtime/engine/script_context.hpp"

namespace webcli {
std::string textarea_control_value(ScriptContext& state, lxb_dom_node_t* node) {
  const auto stored = state.dom.textarea_values.find(node);
  auto value = stored == state.dom.textarea_values.end() ? node_text(node) : stored->second;
  // The API value normalizes CR and CRLF to LF without changing default text.
  std::string normalized;
  normalized.reserve(value.size());
  for (std::size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '\r') {
      normalized += '\n';
      if (i + 1 < value.size() && value[i + 1] == '\n') ++i;
    } else {
      normalized += value[i];
    }
  }
  return normalized;
}
JSValue js_dom_textarea_value(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  auto* node = argc ? dom_node_arg(context, argv[0]) : nullptr;
  if (!node || tag_name(node) != "textarea") return JS_ThrowTypeError(context, "textarea required");
  auto& state = *static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  if (argc > 1) {
    auto value = js_string(context, argv[1]);
    auto& values = state.dom.textarea_values;
    const auto existing = values.find(node);
    const auto previous_bytes = existing == values.end() ? 0 : existing->second.size();
    state.dom.allocations.reserve(value.size() > previous_bytes ? value.size() - previous_bytes
                                                                : 0);
    const auto bytes = value.size();
    values.insert_or_assign(node, std::move(value));
    if (previous_bytes > bytes) state.dom.allocations.release_bytes(previous_bytes - bytes);
    ++state.css.style_generation;
  }
  const auto value = textarea_control_value(state, node);
  return JS_NewStringLen(context, value.data(), value.size());
}
namespace {
std::vector<lxb_dom_node_t*> select_options(lxb_dom_node_t* root) {
  std::vector<lxb_dom_node_t*> options;
  auto* node = root->first_child;
  while (node) {
    if (tag_name(node) == "option") options.push_back(node);
    if (node->first_child) {
      node = node->first_child;
      continue;
    }
    while (node != root && !node->next) node = node->parent;
    if (node == root) break;
    node = node->next;
  }
  return options;
}
lxb_dom_node_t* current_option(ScriptContext& state, lxb_dom_node_t* root,
                               const std::vector<lxb_dom_node_t*>& options) {
  const auto explicit_selection = state.dom.selected_options.find(root);
  if (explicit_selection != state.dom.selected_options.end()) {
    auto* selected = explicit_selection->second;
    if (!selected || std::find(options.begin(), options.end(), selected) != options.end())
      return selected;
    state.dom.selected_options.erase(explicit_selection);
  }
  lxb_dom_node_t* selected = nullptr;
  for (auto* option : options)
    if (has_attr(option, "selected")) selected = option;
  if (selected) return selected;
  if (!has_attr(root, "multiple"))
    for (auto* option : options)
      if (!has_attr(option, "disabled") &&
          !(tag_name(option->parent) == "optgroup" && has_attr(option->parent, "disabled")))
        return option;
  return nullptr;
}
}  // namespace
JSValue js_dom_selected_index(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  auto* state = static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  auto* node = argc ? dom_node_arg(context, argv[0]) : nullptr;
  if (!node || tag_name(node) != "select") return JS_ThrowTypeError(context, "select required");
  const auto options = select_options(node);
  if (argc > 1) {
    int32_t index = -1;
    if (JS_ToInt32(context, &index, argv[1]) < 0) return JS_EXCEPTION;
    state->dom.selected_options[node] =
        index >= 0 && static_cast<std::size_t>(index) < options.size() ? options[index] : nullptr;
    ++state->css.style_generation;
  }
  auto* selected = current_option(*state, node, options);
  const auto found = std::find(options.begin(), options.end(), selected);
  return JS_NewInt32(context,
                     found == options.end() ? -1 : static_cast<int>(found - options.begin()));
}
std::pair<bool, std::string> selected_control_value(ScriptContext& state, lxb_dom_node_t* root) {
  auto* selected = current_option(state, root, select_options(root));
  if (!selected) return {false, ""};
  const bool successful =
      !has_attr(selected, "disabled") &&
      !(tag_name(selected->parent) == "optgroup" && has_attr(selected->parent, "disabled"));
  return {successful,
          has_attr(selected, "value") ? attr(selected, "value") : trim(node_text(selected))};
}

}  // namespace webcli
