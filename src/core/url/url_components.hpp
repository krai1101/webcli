#pragma once
#include <lexbor/url/url.h>

#include <string>
namespace webcli {
std::string prepare_url_input(std::string);
void normalize_url_components(lxb_url_t*);
lxb_status_t set_url_host(lxb_url_t*, lxb_url_parser_t*, const std::string&);
}  // namespace webcli
