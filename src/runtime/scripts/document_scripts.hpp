#pragma once

#include <lexbor/dom/interfaces/node.h>

#include <cstddef>
#include <string>
#include <vector>

namespace webcli {
struct ScriptContext;
struct DocumentScript {
  std::string source, url;
  bool module = false;
  lxb_dom_node_t* node = nullptr;
  bool import_map = false;
  std::string failure{};
  std::string base_url{}, referrer_policy{};
};

struct DocumentScriptOrder {
  std::vector<std::size_t> indices;
  std::size_t parsing_end = 0;
};

void collect_document_scripts(ScriptContext&, lxb_dom_node_t*, const std::string&,
                              std::vector<DocumentScript>&, int depth = 0);
bool is_async_document_script(const DocumentScript&);
DocumentScriptOrder order_document_scripts(const std::vector<DocumentScript>&);
}  // namespace webcli
