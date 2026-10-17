#pragma once
#include <lexbor/html/html.h>

#include <memory>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "runtime/dom/allocation_budget.hpp"
namespace webcli {
struct ParsedDocumentOwner;
// Document-local native tree bookkeeping. The live document is borrowed from
// BrowsingInstance; detached parsed documents have collectable JS ownership.
struct DomState {
  using ParsedDocument = std::unique_ptr<lxb_html_document_t, decltype(&lxb_html_document_destroy)>;
  std::unordered_map<lxb_dom_document_t*, ParsedDocumentOwner*> parsed_documents;
  std::vector<int> free_node_ids;
  DomAllocationBudget allocations;
  std::unordered_map<lxb_dom_node_t*, int> node_ids;
  // Null selection records an explicit clearing of the current value.
  std::unordered_map<lxb_dom_node_t*, lxb_dom_node_t*> selected_options;
  // An absent entry means the textarea still follows its default child text.
  std::unordered_map<lxb_dom_node_t*, std::string> textarea_values;
  // HTML parser association survives table foster parenting until a DOM move.
  std::unordered_map<lxb_dom_node_t*, lxb_dom_node_t*> parser_form_owners;
  std::unordered_set<lxb_dom_node_t*> open_popovers;
  // Opening order, rather than document order, determines the active dialog.
  std::unordered_map<lxb_dom_node_t*, std::uint64_t> modal_dialogs;
  std::uint64_t modal_sequence = 0;
  std::unordered_map<lxb_dom_node_t*, lxb_dom_node_t*> shadow_roots;
  std::unordered_map<lxb_dom_node_t*, lxb_dom_node_t*> shadow_hosts;
  std::unordered_map<lxb_dom_node_t*, lxb_dom_node_t*> assigned_slots;
  std::unordered_map<lxb_dom_node_t*, std::vector<lxb_dom_node_t*>> slot_children;
  lxb_html_document_t* document = nullptr;
  std::vector<lxb_dom_node_t*> nodes;
};
}  // namespace webcli
