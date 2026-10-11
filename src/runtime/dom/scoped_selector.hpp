#pragma once
#include <lexbor/html/html.h>

#include <cctype>
#include <string>
#include <utility>

#include "core/errors.hpp"
#include "runtime/dom/dom_helpers.hpp"
namespace webcli {
// Lexbor does not parse :scope. Replace its unquoted tokens with a temporary
// identity selector; remove the native marker before returning to JavaScript.
class ScopedSelector {
 public:
  ScopedSelector(lxb_dom_node_t* root, std::string input) : selector(std::move(input)) {
    if (root->type == LXB_DOM_NODE_TYPE_DOCUMENT)
      for (auto* child = root->first_child; child; child = child->next)
        if (tag_name(child) == "html") {
          root = child;
          break;
        }
    name_ = "data-webcli-query-scope";
    while (has_attr(root, name_.c_str())) name_ += '-';
    std::string output;
    char quote = 0;
    bool replaced = false;
    for (std::size_t i = 0; i < selector.size(); ++i) {
      char c = selector[i];
      if (c == '\\') {
        output += c;
        if (i + 1 < selector.size()) output += selector[++i];
        continue;
      }
      if (quote) {
        output += c;
        if (c == quote) quote = 0;
        continue;
      }
      if (c == '\'' || c == '"') {
        quote = c;
        output += c;
        continue;
      }
      if (c == ':' && i + 6 <= selector.size()) {
        std::string token = selector.substr(i, 6);
        for (char& value : token)
          value = static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
        if (token == ":scope" && (i + 6 == selector.size() ||
                                  (!std::isalnum(static_cast<unsigned char>(selector[i + 6])) &&
                                   selector[i + 6] != '-' && selector[i + 6] != '_'))) {
          output += '[' + name_ + ']';
          i += 5;
          replaced = true;
          continue;
        }
      }
      output += c;
    }
    if (!replaced) return;
    selector = std::move(output);
    if (root->type != LXB_DOM_NODE_TYPE_ELEMENT) return;
    auto* result = lxb_dom_element_set_attribute(
        lxb_dom_interface_element(root), reinterpret_cast<const lxb_char_t*>(name_.data()),
        name_.size(), reinterpret_cast<const lxb_char_t*>(""), 0);
    if (!result) throw CliError("SELECTOR_SCOPE", "could not establish selector scope");
    scope_ = root;
  }
  ~ScopedSelector() {
    if (scope_)
      lxb_dom_element_remove_attribute(lxb_dom_interface_element(scope_),
                                       reinterpret_cast<const lxb_char_t*>(name_.data()),
                                       name_.size());
  }
  ScopedSelector(const ScopedSelector&) = delete;
  ScopedSelector& operator=(const ScopedSelector&) = delete;
  std::string selector;

 private:
  lxb_dom_node_t* scope_ = nullptr;
  std::string name_;
};
}  // namespace webcli
