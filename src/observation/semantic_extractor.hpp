#pragma once

#include <cstdint>
#include <string>
#include <lexbor/html/html.h>

#include <map>
#include <tuple>

#include "core/page.hpp"
namespace webcli {
class JsRuntime;
class SemanticExtractor {
 public:
  void extract(lxb_html_document_t*, Page&, JsRuntime&);

 private:
  std::map<std::tuple<int, std::uint64_t, int, char>, std::string> refs_;
  std::map<char, std::size_t> counts_;
  std::string ref(int frame, std::uint64_t generation, int node, char role);
};
}  // namespace webcli
