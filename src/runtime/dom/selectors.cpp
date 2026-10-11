#include <unordered_set>

#include "runtime/dom/dom_bindings.hpp"
#include "runtime/dom/scoped_selector.hpp"
#include "runtime/engine/js_handles.hpp"
#include "runtime/engine/script_context.hpp"

namespace webcli {
namespace {
struct SelectorResult {
  std::vector<int> ids;
  std::unordered_set<int> seen;
  ScriptContext* state;
  lxb_dom_node_t* excluded;
};
lxb_status_t collect_selector_result(lxb_dom_node_t* node, lxb_css_selector_specificity_t,
                                     void* opaque) noexcept {
  try {
    auto* result = static_cast<SelectorResult*>(opaque);
    if (node == result->excluded) return LXB_STATUS_OK;
    const int id = dom_node_id(result->state, node);
    if (result->seen.insert(id).second) result->ids.push_back(id);
    return LXB_STATUS_OK;
  } catch (...) {
    return LXB_STATUS_ERROR;
  }
}

}  // namespace

JSValue js_dom_query(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  if (argc < 2) return JS_ThrowTypeError(context, "root id and selector are required");
  auto* state = static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  auto* root = dom_node_arg(context, argv[0]);
  const JsCString selector_owner(context, argv[1]);
  const char* selector = selector_owner.get();
  if (!root) return JS_ThrowTypeError(context, "invalid query root");
  ScopedSelector scoped(root, selector);
  auto* list = lxb_css_selectors_parse(state->css.css_parser,
                                       reinterpret_cast<const lxb_char_t*>(scoped.selector.data()),
                                       scoped.selector.size());

  if (!list) return JS_ThrowSyntaxError(context, "invalid CSS selector: %.4096s", selector);
  const bool match = argc > 2 && JS_ToBool(context, argv[2]) > 0;
  SelectorResult found{
      {}, {}, state, !match && root->type == LXB_DOM_NODE_TYPE_ELEMENT ? root : nullptr};
  lxb_selectors_opt_set(state->css.selectors, root->type == LXB_DOM_NODE_TYPE_ELEMENT
                                                  ? LXB_SELECTORS_OPT_MATCH_ROOT
                                                  : LXB_SELECTORS_OPT_DEFAULT);
  const auto status =
      match ? lxb_selectors_match_node(state->css.selectors, root, list, collect_selector_result,
                                       &found)
            : lxb_selectors_find(state->css.selectors, root, list, collect_selector_result, &found);
  lxb_css_selector_list_destroy(list);
  if (status != LXB_STATUS_OK) return JS_ThrowInternalError(context, "DOM selector query failed");
  if (match) return JS_NewBool(context, !found.ids.empty());
  JSValue output = JS_NewArray(context);
  for (uint32_t i = 0; i < found.ids.size(); ++i)
    JS_SetPropertyUint32(context, output, i, JS_NewInt32(context, found.ids[i]));
  return output;
}

}  // namespace webcli
