#include <algorithm>
#include <cstring>

#include "runtime/dom/dom_bindings.hpp"
#include "runtime/dom/dom_helpers.hpp"
#include "runtime/dom/html_parser.hpp"
#include "runtime/dom/namespace_bindings.hpp"
#include "runtime/engine/js_handles.hpp"
#include "runtime/engine/script_context.hpp"
#include "runtime/frames/frame_tree.hpp"

namespace webcli {
namespace {
lxb_dom_attr_t* qualified_attribute(lxb_dom_node_t* node, std::string name) {
  if (!node || node->type != LXB_DOM_NODE_TYPE_ELEMENT) return nullptr;
  const bool insensitive =
      node->ns == LXB_NS_HTML && node->owner_document->type == LXB_DOM_DOCUMENT_DTYPE_HTML;
  if (insensitive)
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; });
  for (auto* attribute = lxb_dom_interface_element(node)->first_attr; attribute;
       attribute = attribute->next) {
    std::size_t length = 0;
    const auto* raw = lxb_dom_attr_qualified_name(attribute, &length);
    if (raw && std::string_view(reinterpret_cast<const char*>(raw), length) == name)
      return attribute;
  }
  return nullptr;
}
}  // namespace
JSValue js_dom_attr_get(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  if (argc < 2) return JS_ThrowTypeError(context, "node id and attribute name are required");
  auto* node = dom_node_arg(context, argv[0]);
  const JsCString name_owner(context, argv[1]);
  const char* name = name_owner.get();
  if (!node || node->type != LXB_DOM_NODE_TYPE_ELEMENT) {
    return JS_NULL;
  }
  size_t length = 0;
  auto* attribute = qualified_attribute(node, name);
  const auto* value = attribute ? lxb_dom_attr_value(attribute, &length) : nullptr;
  const bool present = attribute != nullptr;

  return present
             ? JS_NewStringLen(context, value ? reinterpret_cast<const char*>(value) : "", length)
             : JS_NULL;
}

JSValue js_dom_attr_set(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  if (argc < 3)
    return JS_ThrowTypeError(context, "node id, attribute name, and value are required");
  auto* node = dom_node_arg(context, argv[0]);
  if (!node || node->type != LXB_DOM_NODE_TYPE_ELEMENT)
    return JS_ThrowTypeError(context, "attributes require an element");
  const JsCString name_owner(context, argv[1]), value_owner(context, argv[2]);
  const char *name = name_owner.get(), *value = value_owner.get();
  auto* state = static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  auto* existing = qualified_attribute(node, name);
  if (existing) {
    std::size_t previous_length = 0;
    const auto* previous_value = lxb_dom_attr_value(existing, &previous_length);
    auto* document = existing->node.owner_document;
    static_assert(
        LEXBOR_VERSION_MAJOR == 3 && LEXBOR_VERSION_MINOR == 0 && LEXBOR_VERSION_PATCH == 1,
        "Review attribute callback buffer ownership before upgrading Lexbor");
    // Lexbor 3.0.1 returns from its HTML attribute-change callback before the
    // old-buffer cleanup in attr_set_value. These synchronous built-in callbacks
    // borrow the old value; reclaim it after they have returned. The no-callback
    // path performs this cleanup itself.
    const bool callback_path = !(lxb_dom_document_opt(document) & LXB_DOM_DOCUMENT_OPT_WO_EVENTS) &&
                               document->attr_mutation->change != nullptr &&
                               existing->owner != nullptr;
    // The existing name is retained; only the new value is allocated. Reserve
    // peak space first, then account for Lexbor freeing the replaced value.
    state->dom.allocations.reserve(std::strlen(value));
    if (lxb_dom_attr_set_value(existing, reinterpret_cast<const lxb_char_t*>(value),
                               std::strlen(value)) != LXB_STATUS_OK)
      return JS_ThrowOutOfMemory(context);
    if (callback_path && previous_value)
      lexbor_mraw_free(document->text, const_cast<lxb_char_t*>(previous_value));
    release_attribute_bytes(*state, node, previous_length);
    if (tag_name(node) == "iframe" && (std::string(name) == "src" || std::string(name) == "srcdoc"))
      state->frame_tree->changed(*state,node);
    return JS_UNDEFINED;
  }
  state->dom.allocations.reserve(std::strlen(name) + std::strlen(value), 1);
  if (node->ns != LXB_NS_HTML) {
    auto* attribute = lxb_dom_attr_interface_create(node->owner_document);
    if (!attribute) return JS_ThrowOutOfMemory(context);
    if (lxb_dom_attr_set_name(attribute, reinterpret_cast<const lxb_char_t*>(name),
                              std::strlen(name), false) != LXB_STATUS_OK ||
        lxb_dom_attr_set_value(attribute, reinterpret_cast<const lxb_char_t*>(value),
                               std::strlen(value)) != LXB_STATUS_OK ||
        lxb_dom_element_attr_append(lxb_dom_interface_element(node), attribute) != LXB_STATUS_OK) {
      lxb_dom_attr_interface_destroy(attribute);
      return JS_ThrowOutOfMemory(context);
    }
    attribute->node.ns = LXB_NS__UNDEF;
    return JS_UNDEFINED;
  }
  auto* result = lxb_dom_element_set_attribute(
      lxb_dom_interface_element(node), reinterpret_cast<const lxb_char_t*>(name), std::strlen(name),
      reinterpret_cast<const lxb_char_t*>(value), std::strlen(value));

  if (result) result->node.ns = LXB_NS__UNDEF;
  if (result && tag_name(node) == "iframe" && (std::string(name) == "src" || std::string(name) == "srcdoc"))
    state->frame_tree->changed(*state,node);
  return result ? JS_UNDEFINED : JS_ThrowInternalError(context, "could not set DOM attribute");
}

JSValue js_dom_attr_remove(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  if (argc < 2) return JS_ThrowTypeError(context, "node id and attribute name are required");
  auto* node = dom_node_arg(context, argv[0]);
  if (!node || node->type != LXB_DOM_NODE_TYPE_ELEMENT)
    return JS_ThrowTypeError(context, "attributes require an element");
  const JsCString name_owner(context, argv[1]);
  const char* name = name_owner.get();
  if (auto* attribute = qualified_attribute(node, name)) lxb_dom_attr_remove(attribute);
  auto* state = static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  if (tag_name(node) == "iframe" && (std::string(name) == "src" || std::string(name) == "srcdoc"))
    state->frame_tree->changed(*state,node);
  return JS_UNDEFINED;
}

JSValue js_dom_attributes(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  auto* node = argc ? dom_node_arg(context, argv[0]) : nullptr;
  JSValue result = JS_NewArray(context);
  if (!node || node->type != LXB_DOM_NODE_TYPE_ELEMENT) return result;
  uint32_t i = 0;
  for (auto* attribute = lxb_dom_interface_element(node)->first_attr; attribute;
       attribute = attribute->next) {
    std::size_t name_length = 0, value_length = 0;
    const auto* name = lxb_dom_attr_qualified_name(attribute, &name_length);
    const auto* value = lxb_dom_attr_value(attribute, &value_length);
    JSValue entry = JS_NewObject(context);
    JS_SetPropertyStr(context, entry, "name",
                      JS_NewStringLen(context, reinterpret_cast<const char*>(name), name_length));
    const auto uri = dom_namespace_uri(lxb_dom_interface_node(attribute));
    const std::string_view qualified(reinterpret_cast<const char*>(name), name_length);
    const auto colon =
        attribute->node.prefix == LXB_NS__UNDEF ? std::string_view::npos : qualified.find(':');
    const auto local = colon == std::string_view::npos ? qualified : qualified.substr(colon + 1);
    JS_SetPropertyStr(context, entry, "namespaceURI",
                      uri.empty() ? JS_NULL : JS_NewStringLen(context, uri.data(), uri.size()));
    JS_SetPropertyStr(context, entry, "localName",
                      JS_NewStringLen(context, local.data(), local.size()));
    JS_SetPropertyStr(
        context, entry, "value",
        JS_NewStringLen(context, value ? reinterpret_cast<const char*>(value) : "", value_length));
    JS_SetPropertyUint32(context, result, i++, entry);
  }
  return result;
}

}  // namespace webcli
