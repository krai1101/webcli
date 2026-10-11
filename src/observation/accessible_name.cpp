#include "observation/accessible_name.hpp"

#include <set>
#include <sstream>
#include <algorithm>
#include <cctype>

#include "core/text.hpp"
#include "runtime/dom/dom_helpers.hpp"
#include "runtime/engine/js_runtime.hpp"

namespace webcli {
std::string referenced_name(lxb_dom_node_t* node, lxb_dom_node_t* scope,
                            const LabelReferences& references, JsRuntime& runtime,
                            std::set<lxb_dom_node_t*>& consumed_labels) {
  std::istringstream tokens(attr(node, "aria-labelledby"));
  std::string id, result;
  std::set<lxb_dom_node_t*> visited;
  while (tokens >> id) {
    const auto found = references.find({scope, id});
    if (found == references.end() || !visited.insert(found->second).second) continue;
    const auto label = trim(runtime.composed_text(found->second));
    if (label.empty()) continue;
    auto aria_hidden = attr(found->second, "aria-hidden");
    std::transform(aria_hidden.begin(), aria_hidden.end(), aria_hidden.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    // A referenced tooltip supplies the action name; do not repeat its label
    // as standalone content. Other displayed aria-hidden content remains readable.
    if (aria_hidden == "true") consumed_labels.insert(found->second);
    if (!result.empty()) result += ' ';
    result += label;
    if (result.size() >= 180) break;
  }
  return result.empty() ? trim(attr(node, "aria-label")) : truncate(result, 180);
}
}  // namespace webcli
