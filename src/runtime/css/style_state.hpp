#pragma once
#include <lexbor/css/css.h>
#include <lexbor/selectors/selectors.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "runtime/css/stylesheet_loader.hpp"
#include "runtime/css/stylesheet_resources.hpp"
#include "runtime/css/visibility_rules.hpp"
namespace webcli {
struct InlineStylesheet {
  std::string original, source;
};
// Visibility caches and stylesheet attachments share a single revision domain.
struct StyleState {
  VisibilityValues visibility_values;
  std::unordered_map<lxb_dom_node_t*, std::string> adopted_stylesheets;
  std::size_t adopted_stylesheet_bytes = 0;
  std::uint64_t style_generation = 1, style_resolved_generation = 0;
  bool style_truncated = false;
  LoadedStylesheets stylesheets;
  StylesheetResources stylesheet_resources;
  std::unordered_map<lxb_dom_node_t*, InlineStylesheet> inline_stylesheets;
  std::unordered_set<lxb_dom_node_t*> disabled_stylesheets;
  std::size_t inline_stylesheet_bytes = 0;
  lxb_css_parser_t* css_parser = nullptr;
  lxb_selectors_t* selectors = nullptr;
};
}  // namespace webcli
