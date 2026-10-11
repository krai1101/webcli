#pragma once
#include <lexbor/dom/interfaces/node.h>

#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>

#include "core/page.hpp"

namespace webcli {
struct LoadedStylesheet {
  std::string href;
  std::shared_ptr<const std::string> source;
};
using LoadedStylesheets = std::unordered_map<lxb_dom_node_t*, LoadedStylesheet>;
LoadedStylesheets load_visibility_stylesheets(lxb_dom_node_t*, const std::string&, Page&,
                                              const LoadedStylesheets* previous = nullptr,
                                              const std::string& referrer_policy = "",
                                              std::shared_ptr<std::atomic<bool>> cancelled = {});
}  // namespace webcli
