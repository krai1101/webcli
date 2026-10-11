#pragma once
#include <lexbor/html/html.h>

#include <string>
#include <string_view>
namespace webcli {
void parse_bounded_document(lxb_html_document_t*, std::string_view);
bool is_plain_text_content_type(std::string_view);
std::string plain_text_document(std::string_view);
}  // namespace webcli
