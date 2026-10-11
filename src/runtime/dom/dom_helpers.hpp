#pragma once

#include <lexbor/html/html.h>

#include <cstddef>
#include <string>

namespace webcli {
bool has_class(lxb_dom_node_t*, const char*);

std::string tag_name(lxb_dom_node_t*);
std::string attr(lxb_dom_node_t*, const char*);
bool has_attr(lxb_dom_node_t*, const char*);
bool is_disabled_control(lxb_dom_node_t*);
bool is_content_editable(lxb_dom_node_t*);
std::string control_type(lxb_dom_node_t*);
bool ignored_subtree(lxb_dom_node_t*, bool inline_style = true, bool accessibility = true);
std::string node_text(lxb_dom_node_t*);
std::string css_path(lxb_dom_node_t*);
void collect_text(lxb_dom_node_t*, std::string&, int depth = 0);
void find_title(lxb_dom_node_t*, std::string&, int depth = 0);
bool is_html_content_type(const char*);

}  // namespace webcli
