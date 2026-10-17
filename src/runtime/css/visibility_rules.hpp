#pragma once
#include <lexbor/html/html.h>

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
namespace webcli {
struct StyleSource {
  std::string source, media;
};
struct VisibilityValue {
  std::string display, visibility, opacity;
};
using VisibilityValues = std::unordered_map<lxb_dom_node_t*, VisibilityValue>;
bool visibility_media_matches(const std::string&);
// Retain only supported visibility declarations from a fetched stylesheet.
std::string visibility_css_source(const std::string&, bool& truncated);
VisibilityValues stylesheet_visibility(lxb_dom_node_t* scope, const std::vector<lxb_dom_node_t*>&,
                                       const std::vector<StyleSource>&, bool& truncated);
bool stylesheet_display_none(lxb_dom_node_t*, const VisibilityValues&);
bool stylesheet_invisible(lxb_dom_node_t*, const VisibilityValues&,
                          const std::function<lxb_dom_node_t*(lxb_dom_node_t*)>& parent);
}  // namespace webcli
