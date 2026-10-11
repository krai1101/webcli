#pragma once
#include <lexbor/html/html.h>

#include <functional>
#include <map>
#include <set>
#include <vector>

#include "core/page.hpp"

namespace webcli {
class JsRuntime;
struct ContentProjection {
  JsRuntime& runtime;
  const std::map<lxb_dom_node_t*, std::string>& references;
  const std::map<lxb_dom_node_t*, std::string>& labels;
  const std::set<lxb_dom_node_t*>& consumed_labels;
  const std::set<lxb_dom_node_t*>& suppressed_links;
  const std::vector<lxb_dom_node_t*>& nodes;
  std::function<bool(lxb_dom_node_t*)> ignored;
  std::function<std::vector<ContentBlock>(lxb_dom_node_t*)> frame;
};
void project_content(lxb_dom_node_t*, Page&, const ContentProjection&);
}  // namespace webcli
