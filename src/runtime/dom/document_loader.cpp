#include "runtime/dom/document_loader.hpp"

#include <algorithm>
#include <cctype>

#include "core/errors.hpp"
#include "runtime/dom/tree_usage.hpp"
namespace webcli {
bool is_plain_text_content_type(std::string_view type) {
  type = type.substr(0, type.find(';'));
  while (!type.empty() && std::isspace(static_cast<unsigned char>(type.back())))
    type.remove_suffix(1);
  while (!type.empty() && std::isspace(static_cast<unsigned char>(type.front())))
    type.remove_prefix(1);
  constexpr std::string_view plain = "text/plain";
  return type.size() == plain.size() &&
         std::equal(type.begin(), type.end(), plain.begin(), [](unsigned char a, unsigned char b) {
           return std::tolower(a) == std::tolower(b);
         });
}
std::string plain_text_document(std::string_view text) {
  std::string result = "<!doctype html><html><head><meta charset=\"utf-8\"></head><body><pre>";
  result.reserve(result.size() + text.size() + 32);
  for (const char value : text) {
    switch (value) {
      case '&':
        result += "&amp;";
        break;
      case '<':
        result += "&lt;";
        break;
      case '>':
        result += "&gt;";
        break;
      default:
        result += value;
    }
  }
  result += "</pre></body></html>";
  return result;
}
void parse_bounded_document(lxb_html_document_t* document, std::string_view source) {
  if (source.size() >= 3 && source.substr(0, 3) == "\xEF\xBB\xBF") source.remove_prefix(3);
  if (lxb_html_document_parse_chunk_begin(document) != LXB_STATUS_OK)
    throw CliError("HTML_PARSE_FAILED", "cannot begin HTML parsing");
  constexpr std::size_t chunk_bytes = 16 * 1024;
  for (std::size_t offset = 0; offset < source.size(); offset += chunk_bytes) {
    const auto length = std::min(chunk_bytes, source.size() - offset);
    if (lxb_html_document_parse_chunk(document,
                                      reinterpret_cast<const lxb_char_t*>(source.data() + offset),
                                      length) != LXB_STATUS_OK)
      throw CliError("HTML_PARSE_FAILED", "Lexbor could not parse the document");
    // Stop oversized trees during parsing, before the full response becomes a DOM.
    measure_tree(lxb_dom_interface_node(&document->dom_document));
  }
  if (lxb_html_document_parse_chunk_end(document) != LXB_STATUS_OK)
    throw CliError("HTML_PARSE_FAILED", "cannot finish HTML parsing");
  measure_tree(lxb_dom_interface_node(&document->dom_document));
}
}  // namespace webcli
