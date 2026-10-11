#include "runtime/dom/tree_usage.hpp"

#include <lexbor/dom/dom.h>
#include <lexbor/html/interfaces/template_element.h>

#include <vector>

#include "runtime/dom/allocation_budget.hpp"
#include "runtime/dom/dom_helpers.hpp"
namespace webcli {
TreeUsage measure_tree(lxb_dom_node_t* root, bool include_root) {
  TreeUsage usage;
  std::vector<lxb_dom_node_t*> pending;
  if (include_root)
    pending.push_back(root);
  else
    for (auto* child = root->first_child; child; child = child->next) pending.push_back(child);
  while (!pending.empty()) {
    auto* node = pending.back();
    pending.pop_back();
    if (++usage.nodes > DomAllocationBudget::max_nodes)
      throw CliError("DOM_QUOTA", "DOM tree exceeds the node quota");
    if (node->type == LXB_DOM_NODE_TYPE_TEXT || node->type == LXB_DOM_NODE_TYPE_COMMENT)
      usage.bytes += lxb_dom_interface_character_data(node)->data.length;
    if (node->type == LXB_DOM_NODE_TYPE_ELEMENT)
      for (auto* attribute = lxb_dom_interface_element(node)->first_attr; attribute;
           attribute = attribute->next) {
        if (++usage.nodes > DomAllocationBudget::max_nodes)
          throw CliError("DOM_QUOTA", "DOM attributes exceed the node quota");
        std::size_t length = 0;
        lxb_dom_attr_value(attribute, &length);
        usage.bytes += length;
        lxb_dom_attr_qualified_name(attribute, &length);
        usage.bytes += length;
      }
    if (usage.bytes > DomAllocationBudget::max_bytes)
      throw CliError("DOM_QUOTA", "DOM tree exceeds the byte quota");
    if (node->ns == LXB_NS_HTML && tag_name(node) == "template") {
      auto* content = lxb_html_interface_template(node)->content;
      if (content) pending.push_back(lxb_dom_interface_node(content));
    }
    for (auto* child = node->first_child; child; child = child->next) pending.push_back(child);
  }
  return usage;
}
}  // namespace webcli
