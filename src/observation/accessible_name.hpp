#pragma once

#include <map>
#include <set>
#include <string>

#include <lexbor/html/html.h>

namespace webcli {
class JsRuntime;
using LabelReferences = std::map<std::pair<lxb_dom_node_t*, std::string>, lxb_dom_node_t*>;
// Explicit label references may name hidden nodes, but cannot cross DOM scopes.
std::string referenced_name(lxb_dom_node_t*, lxb_dom_node_t*, const LabelReferences&, JsRuntime&,
                            std::set<lxb_dom_node_t*>& consumed_labels);
}  // namespace webcli
