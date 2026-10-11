#pragma once
#include <lexbor/dom/interfaces/document.h>
#include <unordered_map>

namespace webcli {
struct ScriptContext;
using NodeCopies = std::unordered_map<lxb_dom_node_t*, lxb_dom_node_t*>;
// Copy into the destination arena. Shadow trees follow adoption, but not cloneNode.
NodeCopies clone_native_tree(ScriptContext&, lxb_dom_node_t*, lxb_dom_document_t*,
                             bool deep, bool include_shadow);
}  // namespace webcli
