
#include "runtime/dom/dom_helpers.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "core/errors.hpp"
#include "core/page.hpp"
#include "core/text.hpp"
#include "network/http_client.hpp"

namespace webcli {

bool is_content_editable(lxb_dom_node_t* node) {
  for (auto* current = node; current; current = current->parent) {
    if (!has_attr(current, "contenteditable")) continue;
    auto value = attr(current, "contenteditable");
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (value.empty() || value == "true" || value == "plaintext-only") return true;
    if (value == "false") return false;
  }
  return false;
}

std::string control_type(lxb_dom_node_t* node) {
  auto type = attr(node, "type");
  std::transform(type.begin(), type.end(), type.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  const auto tag = tag_name(node);
  if (tag == "button") return type == "button" || type == "reset" ? type : "submit";
  if (tag == "input") {
    const std::string valid =
        "|button|checkbox|color|date|datetime-local|email|file|hidden|image|month|number|password|"
        "radio|range|reset|search|submit|tel|text|time|url|week|";
    return !type.empty() && valid.find("|" + type + "|") != std::string::npos ? type : "text";
  }
  return type;
}

bool is_disabled_control(lxb_dom_node_t* node) {
  if (has_attr(node, "disabled")) return true;
  for (auto* parent = node ? node->parent : nullptr; parent; parent = parent->parent) {
    if (tag_name(parent) != "fieldset" || !has_attr(parent, "disabled")) continue;
    lxb_dom_node_t* legend = nullptr;
    for (auto* child = parent->first_child; child; child = child->next)
      if (tag_name(child) == "legend") {
        legend = child;
        break;
      }
    bool inside_legend = false;
    for (auto* ancestor = node; ancestor && ancestor != parent; ancestor = ancestor->parent)
      if (ancestor == legend) {
        inside_legend = true;
        break;
      }
    if (!inside_legend) return true;
  }
  return false;
}

bool is_html_content_type(const char* value) {
  if (!value || !*value) return true;
  std::string media_type(value);
  const auto parameters = media_type.find(';');
  if (parameters != std::string::npos) media_type.resize(parameters);
  media_type = trim(media_type);
  std::transform(media_type.begin(), media_type.end(), media_type.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return media_type == "text/html" || media_type == "application/xhtml+xml";
}

std::string tag_name(lxb_dom_node_t* node) {
  if (!node || node->type != LXB_DOM_NODE_TYPE_ELEMENT) return {};
  size_t length = 0;
  const auto* name = lxb_dom_node_name(node, &length);
  if (!name) return {};
  std::string result(reinterpret_cast<const char*>(name), length);
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return result;
}

std::string attr(lxb_dom_node_t* node, const char* name) {
  if (!node || node->type != LXB_DOM_NODE_TYPE_ELEMENT) return {};
  size_t length = 0;
  const auto* value = lxb_dom_element_get_attribute(lxb_dom_interface_element(node),
                                                    reinterpret_cast<const lxb_char_t*>(name),
                                                    std::strlen(name), &length);
  return value ? std::string(reinterpret_cast<const char*>(value), length) : std::string{};
}

std::string css_path(lxb_dom_node_t* node) {
  std::vector<std::string> segments;
  for (auto* current = node; current && current->type == LXB_DOM_NODE_TYPE_ELEMENT;
       current = lxb_dom_node_parent(current)) {
    const auto tag = tag_name(current);
    std::size_t index = 1;
    auto* parent = lxb_dom_node_parent(current);
    if (parent)
      for (auto* sibling = lxb_dom_node_first_child(parent); sibling && sibling != current;
           sibling = lxb_dom_node_next(sibling))
        if (tag_name(sibling) == tag) ++index;
    segments.push_back(tag + ":nth-of-type(" + std::to_string(index) + ")");
  }
  std::reverse(segments.begin(), segments.end());
  std::string path;
  for (const auto& segment : segments) {
    if (!path.empty()) path.push_back(' ');
    path += segment;
  }
  return path;
}

bool has_attr(lxb_dom_node_t* node, const char* name) {
  return node && node->type == LXB_DOM_NODE_TYPE_ELEMENT &&
         lxb_dom_element_has_attribute(lxb_dom_interface_element(node),
                                       reinterpret_cast<const lxb_char_t*>(name),
                                       std::strlen(name));
}

std::string node_text(lxb_dom_node_t* node) {
  size_t length = 0;
  auto* raw = lxb_dom_node_text_content(node, &length);
  std::string result = raw ? std::string(reinterpret_cast<const char*>(raw), length) : "";
  if (raw) lxb_dom_document_destroy_text(node->owner_document, raw);
  return result;
}

bool has_class(lxb_dom_node_t* node, const char* name) {
  const auto classes = attr(node, "class");
  std::istringstream stream(classes);
  std::string token;
  while (stream >> token) {
    std::transform(token.begin(), token.end(), token.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (token == name) return true;
  }
  return false;
}

bool ignored_subtree(lxb_dom_node_t* node, bool inline_style, bool accessibility) {
  const auto tag = tag_name(node);
  if (tag.empty()) return false;
  if (tag == "script" || tag == "style" || tag == "noscript" || tag == "svg" || tag == "template" ||
      tag == "head")
    return true;
  if (tag_name(node) == "dialog" && !has_attr(node, "open")) return true;
  if (has_attr(node, "hidden")) return true;
  auto aria_hidden = attr(node, "aria-hidden");
  std::transform(aria_hidden.begin(), aria_hidden.end(), aria_hidden.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (accessibility && aria_hidden == "true") return true;
  std::string style = attr(node, "style");
  style.erase(
      std::remove_if(style.begin(), style.end(), [](unsigned char c) { return std::isspace(c); }),
      style.end());
  std::transform(style.begin(), style.end(), style.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (inline_style && (style.find("display:none") != std::string::npos ||
                       style.find("visibility:hidden") != std::string::npos))
    return true;
  return false;
}

void collect_text(lxb_dom_node_t* node, std::string& output, int depth) {
  if (!node || depth > 256 || output.size() > kMaxTextChars * 2) return;
  if (ignored_subtree(node)) return;
  if (node->type == LXB_DOM_NODE_TYPE_TEXT || node->type == LXB_DOM_NODE_TYPE_CDATA_SECTION) {
    size_t length = 0;
    const auto* text = lxb_dom_node_text_content(node, &length);
    if (text) {
      output.append(reinterpret_cast<const char*>(text), length);
      lxb_dom_document_destroy_text(node->owner_document, const_cast<lxb_char_t*>(text));
    }
    output.push_back(' ');
    return;
  }
  if (node->type != LXB_DOM_NODE_TYPE_ELEMENT && node->type != LXB_DOM_NODE_TYPE_DOCUMENT) return;
  const auto tag = tag_name(node);
  const bool heading = tag.size() == 2 && tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6';
  const bool block = node->type == LXB_DOM_NODE_TYPE_ELEMENT &&
                     (tag == "p" || tag == "div" || heading || tag == "li");
  if (block) output.push_back(' ');
  for (auto* child = lxb_dom_node_first_child(node); child; child = lxb_dom_node_next(child))
    collect_text(child, output, depth + 1);
  if (block) output.push_back(' ');
}

void find_title(lxb_dom_node_t* node, std::string& title, int depth) {
  if (!node || depth > 256 || !title.empty()) return;
  if (tag_name(node) == "title") {
    std::string raw;
    collect_text(node, raw);
    title = trim(raw);
    return;
  }
  for (auto* child = lxb_dom_node_first_child(node); child && title.empty();
       child = lxb_dom_node_next(child))
    find_title(child, title, depth + 1);
}

}  // namespace webcli
