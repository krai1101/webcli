#pragma once
#include <lexbor/html/html.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "core/page.hpp"
#include "runtime/css/visibility_rules.hpp"
#include "runtime/navigation/history_action.hpp"
#include "runtime/navigation/navigation_action.hpp"
namespace webcli {
class StorageStore;
struct FrameEnvironment;
struct ScriptContext;
class JsRuntime {
 public:
  JsRuntime(lxb_html_document_t*, Page&, StorageStore&, const std::string& html,
            HistoryContext history = {}, std::shared_ptr<std::atomic<bool>> cancelled = {},
            const FrameEnvironment* frame = nullptr);
  ~JsRuntime();
  JsRuntime(const JsRuntime&) = delete;
  JsRuntime& operator=(const JsRuntime&) = delete;
  bool evaluate(const std::string&, const std::string& origin);
  // A script checkpoint runs promise jobs, without advancing timers or I/O.
  void checkpoint();
  void pump();
  void suspend();
  void resume();
  // Network settling excludes recurring timers and unresolved JS promises.
  bool network_pending() const;
  bool matches(const std::string& selector);
  void sync(Page&);
  std::string document_base_url() const;
  std::pair<bool, std::string> select_value(lxb_dom_node_t*);
  std::string textarea_value(lxb_dom_node_t*);
  void prepare_composition();
  std::string composed_text(lxb_dom_node_t*);
  std::vector<lxb_dom_node_t*> composed_children(lxb_dom_node_t*);
  lxb_dom_node_t* composed_parent(lxb_dom_node_t*);
  int node_id(lxb_dom_node_t*);
  const std::string* stylesheet_source(lxb_dom_node_t*) const;
  const VisibilityValues& visibility_values(bool& truncated);
  std::uint64_t observation_revision() const;
  lxb_dom_node_t* node(int) const;
  std::optional<NavigationAction> take_navigation();
  std::pair<int, int> take_submission();
  std::vector<HistoryAction> take_history_actions();
  void retain_history(std::size_t length, std::size_t offset);
  void restore_history(const std::string&, int, std::size_t length, std::size_t offset,
                       bool dispatch_pop = true);
  void notify_history_traversal();
  ScriptContext& script_context();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace webcli
