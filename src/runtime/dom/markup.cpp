#include <lexbor/html/interfaces/template_element.h>

#include "runtime/dom/dom_bindings.hpp"
#include "runtime/dom/dom_helpers.hpp"
#include "runtime/dom/tree_usage.hpp"
#include "runtime/engine/js_handles.hpp"
#include "runtime/engine/script_context.hpp"

namespace webcli {
JSValue js_dom_html(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  auto* node = argc ? dom_node_arg(context, argv[0]) : nullptr;
  if (!node || (node->type != LXB_DOM_NODE_TYPE_ELEMENT &&
                node->type != LXB_DOM_NODE_TYPE_DOCUMENT_FRAGMENT))
    return JS_ThrowTypeError(context, "element or fragment required");
  auto* context_node = node;
  if (node->ns == LXB_NS_HTML && tag_name(node) == "template")
    node = lxb_dom_interface_node(lxb_html_interface_template(node)->content);
  if (argc > 1) {
    const JsCString html_owner(context, argv[1]);
    const char* html = html_owner.get();
    if (!html) return JS_EXCEPTION;
    auto* state = static_cast<ScriptContext*>(JS_GetContextOpaque(context));
    const auto size = html_owner.size();
    if (size > state->dom.allocations.remaining_bytes())
      return JS_ThrowInternalError(context, "innerHTML exceeds remaining DOM allocation budget");
    using Document = std::unique_ptr<lxb_html_document_t, decltype(&lxb_html_document_destroy)>;
    Document temporary(lxb_html_document_create(), lxb_html_document_destroy);
    if (!temporary) {
      return JS_ThrowOutOfMemory(context);
    }
    lxb_html_document_scripting_set(temporary.get(), context_node->owner_document->scripting);
    auto* fragment =
        lxb_html_document_parse_fragment(temporary.get(),
                                         context_node->type == LXB_DOM_NODE_TYPE_ELEMENT
                                             ? lxb_dom_interface_element(context_node)
                                             : lxb_dom_interface_element(state->dom.document->body),
                                         reinterpret_cast<const lxb_char_t*>(html), size);

    if (!fragment) return JS_ThrowInternalError(context, "innerHTML parse failed");
    const auto usage = measure_tree(fragment, false);
    state->dom.allocations.reserve(size, usage.nodes);
    std::vector<lxb_dom_node_t*> imported;
    for (auto* child = fragment->first_child; child; child = child->next) {
      auto* copy = lxb_dom_document_import_node(node->owner_document, child, true);
      if (!copy) return JS_ThrowOutOfMemory(context);
      imported.push_back(copy);
    }
    // Detached old children stay document-owned so existing JS wrappers remain valid.
    while (auto* child = node->first_child) {
      reset_parser_form_owners(*state, child);
      lxb_dom_node_remove(child);
    }
    for (auto* child : imported) lxb_dom_node_insert_child(node, child);
    return JS_UNDEFINED;
  }
  std::string html;
  auto callback = [](const lxb_char_t* data, size_t size, void* opaque) -> lxb_status_t {
    try {
      auto& text = *static_cast<std::string*>(opaque);
      if (size > DomAllocationBudget::max_bytes - text.size()) return LXB_STATUS_ERROR;
      text.append(reinterpret_cast<const char*>(data), size);
      return LXB_STATUS_OK;
    } catch (...) {
      return LXB_STATUS_ERROR;
    }
  };
  if (lxb_html_serialize_deep_cb(node, callback, &html) != LXB_STATUS_OK)
    return JS_ThrowInternalError(context, "HTML serialization failed");
  return JS_NewStringLen(context, html.data(), html.size());
}

}  // namespace webcli
