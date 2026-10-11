#include "runtime/dom/namespace_bindings.hpp"

#include <lexbor/ns/ns.h>
#include <lexbor/tag/tag.h>

#include <string_view>

#include "runtime/dom/dom_bindings.hpp"
#include "runtime/engine/js_handles.hpp"
#include "runtime/engine/script_context.hpp"

namespace webcli {
namespace {
const lxb_char_t* characters(const std::string& value) {
  return reinterpret_cast<const lxb_char_t*>(value.data());
}
}  // namespace
std::string dom_namespace_uri(lxb_dom_node_t* node) {
  if (node->ns == LXB_NS__UNDEF) return {};
  std::size_t length = 0;
  const auto* name = lxb_ns_by_id(node->owner_document->ns, node->ns, &length);
  return name ? std::string(reinterpret_cast<const char*>(name), length) : std::string{};
}
namespace {
lxb_dom_document_t* document_argument(JSContext* context, JSValueConst argument) {
  auto* node = dom_node_arg(context, argument);
  return node && node->type == LXB_DOM_NODE_TYPE_DOCUMENT ? lxb_dom_interface_document(node)
                                                          : nullptr;
}
lxb_dom_attr_t* find_attribute(lxb_dom_node_t* node, const std::string& uri,
                               const std::string& local) {
  for (auto* attribute = lxb_dom_interface_element(node)->first_attr; attribute;
       attribute = attribute->next) {
    std::size_t length = 0;
    const auto* name = lxb_dom_attr_qualified_name(attribute, &length);
    const std::string_view qualified(name ? reinterpret_cast<const char*>(name) : "", length);
    const auto colon =
        attribute->node.prefix == LXB_NS__UNDEF ? std::string_view::npos : qualified.find(':');
    const auto local_name =
        colon == std::string_view::npos ? qualified : qualified.substr(colon + 1);
    if (name && local_name == local && dom_namespace_uri(lxb_dom_interface_node(attribute)) == uri)
      return attribute;
  }
  return nullptr;
}
}  // namespace

JSValue js_dom_element_identity(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  auto* node = argc ? dom_node_arg(context, argv[0]) : nullptr;
  if (!node || node->type != LXB_DOM_NODE_TYPE_ELEMENT) return JS_NULL;
  const auto uri = dom_namespace_uri(node);
  std::size_t length = 0;
  const auto* qualified = lxb_dom_element_qualified_name(lxb_dom_interface_element(node), &length);
  JsValueOwner result(context, JS_NewObject(context));
  JS_SetPropertyStr(context, result.get(), "namespaceURI",
                    uri.empty() ? JS_NULL : JS_NewStringLen(context, uri.data(), uri.size()));
  JS_SetPropertyStr(context, result.get(), "qualifiedName",
                    JS_NewStringLen(context, reinterpret_cast<const char*>(qualified), length));
  const std::string_view name(reinterpret_cast<const char*>(qualified), length);
  const auto colon = node->prefix == LXB_NS__UNDEF ? std::string_view::npos : name.find(':');
  JS_SetPropertyStr(
      context, result.get(), "localName",
      JS_NewStringLen(context, name.data() + (colon == std::string_view::npos ? 0 : colon + 1),
                      colon == std::string_view::npos ? name.size() : name.size() - colon - 1));
  JS_SetPropertyStr(
      context, result.get(), "prefix",
      colon == std::string_view::npos ? JS_NULL : JS_NewStringLen(context, name.data(), colon));
  return result.release();
}

JSValue js_dom_create_element_ns(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  if (argc < 4)
    return JS_ThrowTypeError(context, "document, namespace, local name and prefix required");
  auto* document = document_argument(context, argv[0]);
  if (!document) return JS_ThrowTypeError(context, "document required");
  const auto uri = js_string(context, argv[1]);
  const auto local = js_string(context, argv[2]);
  const auto prefix = js_string(context, argv[3]);
  auto& state = *static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  state.dom.allocations.reserve(
      uri.size() + local.size() + prefix.size(),
      uri == "http://www.w3.org/1999/xhtml" && local == "template" ? 2 : 1);
  auto* element = lxb_dom_element_create(
      document, characters(local), local.size(), uri.empty() ? nullptr : characters(uri),
      uri.size(), prefix.empty() ? nullptr : characters(prefix), prefix.size(), nullptr, 0, false);
  if (!element) return JS_ThrowOutOfMemory(context);
  // Lexbor indexes tag identities in lowercase. Preserve the DOM qualified
  // spelling separately, including SVG camel-case and arbitrary XML names.
  const auto qualified = prefix.empty() ? local : prefix + ":" + local;
  auto* data = static_cast<lxb_tag_data_t*>(lexbor_hash_insert(
      document->tags, lexbor_hash_insert_raw, characters(qualified), qualified.size()));
  if (!data) {
    lxb_dom_element_destroy(element);
    return JS_ThrowOutOfMemory(context);
  }
  data->tag_id = element->node.local_name;
  element->qualified_name = reinterpret_cast<uintptr_t>(data);
  return JS_NewInt32(context, dom_node_id(&state, lxb_dom_interface_node(element)));
}

JSValue js_dom_attribute_ns(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  if (argc < 3) return JS_ThrowTypeError(context, "element, namespace and local name required");
  auto* node = dom_node_arg(context, argv[0]);
  if (!node || node->type != LXB_DOM_NODE_TYPE_ELEMENT)
    return JS_ThrowTypeError(context, "element required");
  const auto uri = js_string(context, argv[1]), local = js_string(context, argv[2]);
  auto* old = find_attribute(node, uri, local);
  if (argc == 3) {
    if (!old) return JS_NULL;
    std::size_t length = 0;
    const auto* value = lxb_dom_attr_value(old, &length);
    return JS_NewStringLen(context, value ? reinterpret_cast<const char*>(value) : "", length);
  }
  if (JS_IsNull(argv[3])) {
    // Detached attribute memory remains document-owned, like removed nodes.
    if (old) lxb_dom_attr_remove(old);
    return JS_UNDEFINED;
  }
  if (argc < 5) return JS_ThrowTypeError(context, "qualified name and value required");
  const auto qualified = js_string(context, argv[3]), value = js_string(context, argv[4]);
  auto& state = *static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  state.dom.allocations.reserve(uri.size() + qualified.size() + local.size() + value.size(), 1);
  auto* document = node->owner_document;
  uintptr_t namespace_id = LXB_NS__UNDEF;
  if (!uri.empty()) {
    auto* data = lxb_ns_data_by_link(document->ns, characters(uri), uri.size());
    if (!data) {
      // The public element factory registers namespace strings in Lexbor's
      // document-owned namespace table. Avoid depending on private internals.
      state.dom.allocations.reserve(0, 1);
      auto* registration =
          lxb_dom_element_create(document, characters(local), local.size(), characters(uri),
                                 uri.size(), nullptr, 0, nullptr, 0, false);
      if (!registration) return JS_ThrowOutOfMemory(context);
      namespace_id = registration->node.ns;
      lxb_dom_element_destroy(registration);
    } else
      namespace_id = data->ns_id;
  }
  auto* attribute = lxb_dom_attr_interface_create(document);
  if (!attribute) return JS_ThrowOutOfMemory(context);
  if (lxb_dom_attr_set_name(attribute, characters(local), local.size(), false) != LXB_STATUS_OK ||
      lxb_dom_attr_set_value(attribute, characters(value), value.size()) != LXB_STATUS_OK) {
    lxb_dom_attr_interface_destroy(attribute);
    return JS_ThrowOutOfMemory(context);
  }
  auto* name_data = lexbor_hash_insert(document->attrs, lexbor_hash_insert_raw,
                                       characters(qualified), qualified.size());
  if (!name_data) {
    lxb_dom_attr_interface_destroy(attribute);
    return JS_ThrowOutOfMemory(context);
  }
  auto* qualified_data = static_cast<lxb_dom_attr_data_t*>(name_data);
  if (!qualified_data->attr_id)
    qualified_data->attr_id = reinterpret_cast<uintptr_t>(qualified_data);
  attribute->qualified_name = qualified_data->attr_id;
  attribute->node.ns = namespace_id;
  const auto colon = qualified.find(':');
  if (colon != std::string::npos) {
    const auto* prefix = lxb_ns_prefix_append(document->prefix, characters(qualified), colon);
    if (!prefix) {
      lxb_dom_attr_interface_destroy(attribute);
      return JS_ThrowOutOfMemory(context);
    }
    attribute->node.prefix = prefix->prefix_id;
  }
  if (old) lxb_dom_attr_remove(old);
  if (lxb_dom_element_attr_append(lxb_dom_interface_element(node), attribute) != LXB_STATUS_OK) {
    lxb_dom_attr_interface_destroy(attribute);
    if (old) lxb_dom_element_attr_append(lxb_dom_interface_element(node), old);
    return JS_ThrowInternalError(context, "could not attach namespaced attribute");
  }
  return JS_UNDEFINED;
}

}  // namespace webcli
