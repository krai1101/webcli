#pragma once

#include <lexbor/html/html.h>

#include <cstdint>
#include <map>
#include <string>
#include <tuple>

#include "core/page.hpp"
namespace webcli {
class JsRuntime;
class SemanticExtractor {
 public:
  explicit SemanticExtractor(std::map<char, std::size_t>& counts) : counts_(counts) {}
  void extract(lxb_html_document_t*, Page&, JsRuntime&);

 private:
  std::map<std::tuple<int, std::uint64_t, int, char>, std::string> refs_;
  // Owned by the browsing instance: navigation must not reuse an old token.
  std::map<char, std::size_t>& counts_;
  std::string ref(int frame, std::uint64_t generation, int node, char role);
};
}  // namespace webcli
