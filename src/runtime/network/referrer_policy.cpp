#include "runtime/network/referrer_policy.hpp"

#include <sstream>
#include <vector>

#include "network/referrer.hpp"
#include "runtime/dom/dom_bindings.hpp"
#include "runtime/dom/dom_helpers.hpp"
#include "runtime/engine/js_handles.hpp"
#include "runtime/engine/script_context.hpp"

namespace webcli {
namespace {
std::string ascii_lower(std::string value) {
  for (auto& c : value)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
  return value;
}
}  // namespace
std::string element_referrer_policy(const std::string& policy, lxb_dom_node_t* node) {
  const auto value = ascii_lower(attr(node, "referrerpolicy"));
  return valid_referrer_policy(value) ? value : policy;
}
void process_referrer_meta(ScriptContext& state, lxb_dom_node_t* node) {
  if (!node || node->ns != LXB_NS_HTML || tag_name(node) != "meta" ||
      ascii_lower(attr(node, "name")) != "referrer")
    return;
  auto* ancestor = node;
  while (ancestor->parent) ancestor = ancestor->parent;
  if (ancestor != lxb_dom_interface_node(&state.dom.document->dom_document)) return;
  auto content = ascii_lower(attr(node, "content"));
  if (content == "never")
    content = "no-referrer";
  else if (content == "always")
    content = "unsafe-url";
  else if (content == "default")
    content = "strict-origin-when-cross-origin";
  else if (content == "origin-when-crossorigin")
    content = "origin-when-cross-origin";
  if (valid_referrer_policy(content)) state.referrer_policy = content;
}
JSValue js_process_referrer_meta(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  auto& state = *static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  if (!argc) return JS_NewString(context, state.referrer_policy.c_str());
  auto* root = dom_node_arg(context, argv[0]);
  if (!root) return JS_UNDEFINED;
  auto* ancestor = root;
  while (ancestor->parent) ancestor = ancestor->parent;
  if (ancestor != lxb_dom_interface_node(&state.dom.document->dom_document)) return JS_UNDEFINED;
  std::vector<lxb_dom_node_t*> nodes{root};
  while (!nodes.empty()) {
    auto* node = nodes.back();
    nodes.pop_back();
    process_referrer_meta(state, node);
    for (auto* child = node->last_child; child; child = child->prev) nodes.push_back(child);
  }
  return JS_UNDEFINED;
}
JSValue js_document_referrer(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  auto& state = *static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  auto* node = argc ? dom_node_arg(context, argv[0]) : nullptr;
  if (!node || node->type != LXB_DOM_NODE_TYPE_DOCUMENT)
    return JS_ThrowTypeError(context, "Document required");
  const std::string_view value = node == lxb_dom_interface_node(&state.dom.document->dom_document)
                                     ? std::string_view(state.document_referrer)
                                     : std::string_view{};
  return JS_NewStringLen(context, value.empty() ? "" : value.data(), value.size());
}
JSValue js_queue_navigation(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  if (argc < 3) return JS_ThrowTypeError(context, "Navigation arguments required");
  auto& state = *static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  const auto target = resolve_web_url(state.base_url, js_string(context, argv[0]));
  if (!target) return JS_ThrowTypeError(context, "Invalid navigation URL");
  const JsValueOwner global(context, JS_GetGlobalObject(context));
  NavigationAction action;
  action.url = *target;
  action.replace = JS_ToBool(context, argv[1]) > 0;
  action.reload = JS_ToBool(context, argv[2]) > 0;
  action.referrer.source =
      property_string(context, global.get(), "__webcliCurrentURL", state.base_url);
  action.referrer.policy = state.referrer_policy;
  if (argc > 3) {
    auto* anchor = dom_node_arg(context, argv[3]);
    if (anchor && anchor->ns == LXB_NS_HTML && tag_name(anchor) == "a") {
      action.referrer.policy = element_referrer_policy(state.referrer_policy, anchor);
      std::istringstream rel(ascii_lower(attr(anchor, "rel")));
      std::string token;
      while (rel >> token)
        if (token == "noreferrer") action.referrer.policy = "no-referrer";
    }
  }
  state.navigation = std::move(action);
  return JS_UNDEFINED;
}
}  // namespace webcli
