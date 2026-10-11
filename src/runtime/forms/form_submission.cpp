#include "runtime/forms/form_submission.hpp"

#include <unordered_map>
#include <utility>
#include <vector>

#include "core/errors.hpp"
#include "network/http_client.hpp"
#include "runtime/dom/dom_helpers.hpp"
#include "runtime/engine/js_runtime.hpp"

namespace webcli {
namespace {
std::string ascii_lower(std::string value) {
  for (auto& c : value)
    if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
  return value;
}
}  // namespace
std::string normalize_form_method(std::string value) {
  value = ascii_lower(std::move(value));
  return value == "post" || value == "dialog" ? value : "get";
}
std::string normalize_form_encoding(std::string value) {
  value = ascii_lower(std::move(value));
  return value == "multipart/form-data" || value == "text/plain"
             ? value
             : "application/x-www-form-urlencoded";
}
Form collect_submission_form(lxb_dom_node_t* form_node, JsRuntime& runtime,
                             const std::string& document_url) {
  if (!form_node || tag_name(form_node) != "form")
    throw CliError("FORM_NOT_FOUND", "submission target is not a form");
  auto* root = form_node;
  while (root->parent) root = root->parent;
  if (root->type != LXB_DOM_NODE_TYPE_DOCUMENT && !runtime.composed_parent(root))
    throw CliError("FORM_NOT_FOUND", "form is no longer attached");
  std::vector<lxb_dom_node_t*> nodes, stack{root};
  std::unordered_map<std::string, lxb_dom_node_t*> identifiers;
  while (!stack.empty()) {
    auto* node = stack.back();
    stack.pop_back();
    nodes.push_back(node);
    const auto id = attr(node, "id");
    if (!id.empty()) identifiers.emplace(id, node);
    for (auto* child = node->last_child; child; child = child->prev) stack.push_back(child);
  }
  Form result;
  result.node_id = runtime.node_id(form_node);
  const auto action = attr(form_node, "action");
  result.action = action.empty()
                      ? document_url
                      : resolve_web_url(runtime.document_base_url(), action).value_or("");
  result.method = normalize_form_method(attr(form_node, "method"));
  for (auto* node : nodes) {
    const auto tag = tag_name(node);
    if (tag != "input" && tag != "textarea" && tag != "select" && tag != "button") continue;
    lxb_dom_node_t* owner = nullptr;
    if (has_attr(node, "form")) {
      const auto found = identifiers.find(attr(node, "form"));
      if (found != identifiers.end() && tag_name(found->second) == "form") owner = found->second;
    } else {
      for (auto* parent = node->parent; parent; parent = parent->parent)
        if (tag_name(parent) == "form") {
          owner = parent;
          break;
        }
    }
    if (owner != form_node) continue;
    Field field;
    field.node_id = runtime.node_id(node);
    field.name = attr(node, "name");
    field.type = tag == "select" ? "select" : tag == "textarea" ? "textarea" : control_type(node);
    if (field.type.empty()) field.type = tag == "button" ? "submit" : "text";
    field.disabled = is_disabled_control(node);
    field.checked = has_attr(node, "checked");
    field.value = tag == "textarea" ? runtime.textarea_value(node) : attr(node, "value");
    if ((field.type == "checkbox" || field.type == "radio") && !has_attr(node, "value"))
      field.value = "on";
    if (tag == "select") {
      const auto selected = runtime.select_value(node);
      field.has_selected_value = selected.first;
      field.value = selected.second;
    }
    result.fields.push_back(std::move(field));
  }
  return result;
}
FormNavigation prepare_form_navigation(JsRuntime& runtime, int form_id, int button_id,
                                       const std::string& document_url) {
  auto* form_node = runtime.node(form_id);
  const auto form = collect_submission_form(form_node, runtime, document_url);
  auto method = form.method;
  auto enctype = normalize_form_encoding(attr(form_node, "enctype"));
  auto action = form.action;
  if (button_id >= 0) {
    auto* button = runtime.node(button_id);
    if (has_attr(button, "formaction")) {
      const auto override = attr(button, "formaction");
      action = override.empty()
                   ? document_url
                   : resolve_web_url(runtime.document_base_url(), override).value_or("");
    }
    if (has_attr(button, "formmethod")) method = normalize_form_method(attr(button, "formmethod"));
    if (has_attr(button, "formenctype"))
      enctype = normalize_form_encoding(attr(button, "formenctype"));
  }
  if (method != "get" && method != "post")
    throw CliError("UNSUPPORTED_FORM_METHOD", "only GET and POST forms are supported");
  if (method == "post" && !enctype.empty() && enctype != "application/x-www-form-urlencoded")
    throw CliError("UNSUPPORTED_FORM_ENCODING", "only URL-encoded form submission is supported");
  std::string encoded;
  auto add = [&](const std::string& name, const std::string& value) {
    if (!encoded.empty()) encoded += '&';
    const auto normalize = [](const std::string& text) {
      std::string result;
      for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r') {
          result += "\r\n";
          if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
        } else if (text[i] == '\n')
          result += "\r\n";
        else
          result += text[i];
      }
      return result;
    };
    encoded +=
        encode_form_component(normalize(name)) + "=" + encode_form_component(normalize(value));
  };
  for (const auto& field : form.fields) {
    if (field.disabled || field.name.empty() || field.type == "button" || field.type == "reset")
      continue;
    if (field.type == "file")
      throw CliError("UNSUPPORTED_FILE_UPLOAD", "file uploads are unsupported");
    if (field.type == "submit" && field.node_id != button_id) continue;
    if ((field.type == "checkbox" || field.type == "radio") && !field.checked) continue;
    if (field.type == "select" && has_attr(runtime.node(field.node_id), "multiple"))
      throw CliError("UNSUPPORTED_MULTISELECT", "multiple selects are unsupported");
    if (field.type == "select" && !field.has_selected_value) continue;
    add(field.name, field.value);
  }
  if (method == "get") action = form_get_url(action, encoded);
  return {std::move(action), method == "post" ? "POST" : "GET",
          method == "post" ? std::move(encoded) : ""};
}
}  // namespace webcli
