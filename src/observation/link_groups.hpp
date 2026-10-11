#pragma once
#include <lexbor/dom/interfaces/node.h>

#include <set>
#include <string>
#include <tuple>

#include "runtime/dom/dom_helpers.hpp"

namespace webcli {
// Collapse equivalent navigation links inside one semantic row or article.
// Targets in other groups and prose/navigation blocks stay distinct.
class LinkGroups {
 public:
  bool repeated(lxb_dom_node_t* node, const std::string& url, const std::string& label) {
    if (!attr(node, "onclick").empty()) return false;
    auto* row = node->parent;
    for (unsigned depth = 0; row && depth < 16; ++depth, row = row->parent) {
      if (tag_name(row) == "tr" || attr(row, "role") == "row" || tag_name(row) == "article" ||
          attr(row, "role") == "article")
        return !seen_.emplace(row, url, label).second;
      if (tag_name(row) == "table" || tag_name(row) == "body") break;
    }
    return false;
  }

 private:
  std::set<std::tuple<lxb_dom_node_t*, std::string, std::string>> seen_;
};
}  // namespace webcli
