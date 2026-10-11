#pragma once
#include <lexbor/dom/interfaces/node.h>

#include <cstddef>
namespace webcli {
struct TreeUsage {
  std::size_t nodes = 0, bytes = 0;
};
TreeUsage measure_tree(lxb_dom_node_t*, bool include_root = true);
}  // namespace webcli
