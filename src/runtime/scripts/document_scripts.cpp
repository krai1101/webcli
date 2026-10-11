#include "runtime/scripts/document_scripts.hpp"

#include "core/script_types.hpp"
#include "network/data_script.hpp"
#include "network/http_client.hpp"
#include "runtime/dom/dom_helpers.hpp"
#include "runtime/engine/script_context.hpp"

namespace webcli {
void collect_document_scripts(ScriptContext& state, lxb_dom_node_t* node, const std::string& base,
                              std::vector<DocumentScript>& scripts, int depth) {
  if (!node || depth > 256 || scripts.size() >= kMaxDocumentScripts ||
      (node->type != LXB_DOM_NODE_TYPE_ELEMENT && node->type != LXB_DOM_NODE_TYPE_DOCUMENT &&
       node->type != LXB_DOM_NODE_TYPE_DOCUMENT_FRAGMENT))
    return;
  if (tag_name(node) == "script") {
    const auto kind = script_kind(attr(node, "type"));
    if (kind == ScriptKind::Classic && has_attr(node, "nomodule")) return;
    const auto src = attr(node, "src");
    const bool import_map = script_type_essence(attr(node, "type")) == "importmap";
    if (kind != ScriptKind::Data || import_map) {
      if (import_map && !src.empty()) {
        scripts.push_back({"", src, false, node, true});
        return;
      }
      if (!src.empty()) {
        auto resolved =
            is_data_script_url(src) ? std::optional<std::string>(src) : resolve_web_url(base, src);
        if (resolved)
          scripts.push_back({"", *resolved, kind == ScriptKind::Module, node});
        else
          scripts.push_back(
              {"throw new Error('invalid script URL');", {}, kind == ScriptKind::Module, node});
        return;
      }
      size_t length = 0;
      const auto* content = lxb_dom_node_text_content(node, &length);
      std::string source =
          content ? std::string(reinterpret_cast<const char*>(content), length) : std::string{};
      if (content)
        lxb_dom_document_destroy_text(node->owner_document, const_cast<lxb_char_t*>(content));
      if ((import_map || !source.empty()) && source.size() <= kMaxScriptBytes) {
        scripts.push_back({std::move(source), {}, kind == ScriptKind::Module, node, import_map});
      } else if (source.size() > kMaxScriptBytes) {
        scripts.push_back({"",
                           {},
                           false,
                           node,
                           import_map,
                           "SCRIPT_QUOTA: inline script source exceeds the byte limit"});
      }
    }
    return;
  }
  const auto shadow = state.dom.shadow_roots.find(node);
  if (shadow != state.dom.shadow_roots.end())
    collect_document_scripts(state, shadow->second, base, scripts, depth + 1);
  for (auto* child = lxb_dom_node_first_child(node); child && scripts.size() < kMaxDocumentScripts;
       child = lxb_dom_node_next(child))
    collect_document_scripts(state, child, base, scripts, depth + 1);
}

bool is_async_document_script(const DocumentScript& script) {
  return !script.import_map && (script.module || !script.url.empty()) &&
         has_attr(script.node, "async");
}

DocumentScriptOrder order_document_scripts(const std::vector<DocumentScript>& scripts) {
  DocumentScriptOrder order;
  std::vector<std::size_t> deferred;
  for (std::size_t i = 0; i < scripts.size(); ++i) {
    const auto& script = scripts[i];
    const bool defer = !script.import_map && !is_async_document_script(script) &&
                       (script.module || (!script.url.empty() && has_attr(script.node, "defer")));
    (defer ? deferred : order.indices).push_back(i);
  }
  order.parsing_end = order.indices.size();
  order.indices.insert(order.indices.end(), deferred.begin(), deferred.end());
  return order;
}
}  // namespace webcli
