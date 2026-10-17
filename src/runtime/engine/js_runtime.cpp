#include "runtime/wasm/bindings.hpp"
#include "runtime/engine/js_runtime.hpp"

#include <curl/curl.h>
#include <lexbor/css/css.h>
#include <lexbor/selectors/selectors.h>
#include <quickjs.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <future>
#include <thread>
#include <unordered_map>

#include "core/browser_identity.hpp"
#include "core/errors.hpp"
#include "core/load_trace.hpp"
#include "core/runtime_policy.hpp"
#include "core/script_types.hpp"
#include "core/serialization/json_string.hpp"
#include "core/text.hpp"
#include "core/url/url.hpp"
#include "network/data_script.hpp"
#include "network/fetch_metadata.hpp"
#include "network/http_client.hpp"
#include "runtime/css/style_resolver.hpp"
#include "runtime/css/stylesheet_bindings.hpp"
#include "runtime/css/viewport.hpp"
#include "runtime/dom/component_tree.hpp"
#include "runtime/dom/dom_bindings.hpp"
#include "runtime/dom/dom_helpers.hpp"
#include "runtime/dom/html_parser.hpp"
#include "runtime/dom/namespace_bindings.hpp"
#include "runtime/embedded_scripts.hpp"
#include "runtime/engine/diagnostics.hpp"
#include "runtime/engine/js_handles.hpp"
#include "runtime/engine/native_bindings.hpp"
#include "runtime/engine/script_context.hpp"
#include "runtime/events/timers.hpp"
#include "runtime/frames/frame_tree.hpp"
#include "runtime/messaging/broadcast_channels.hpp"
#include "runtime/messaging/message_ports.hpp"
#include "runtime/network/blob_urls.hpp"
#include "runtime/network/completion_delivery.hpp"
#include "runtime/network/resource_tasks.hpp"
#include "runtime/network/web_bindings.hpp"
#include "runtime/page/lifecycle_delivery.hpp"
#include "runtime/scripts/module_loader.hpp"
#include "runtime/scripts/parser_coordinator.hpp"
#include "runtime/workers/worker_manager.hpp"
#include "storage/profile_store.hpp"

namespace webcli {

int interrupt_script(JSRuntime*, void* opaque) {
  return static_cast<ExecutionBudget*>(opaque)->interrupt();
}

struct JsRuntime::Impl {
  std::shared_ptr<ScriptContext> owned_state = std::make_shared<ScriptContext>();
  ScriptContext& state = *owned_state;
  bool owns_runtime = true;
  ~Impl() {
    // The interrupt callback belongs to the shared runtime. Restore a live
    // owner's budget before releasing a child's context and native state.
    if (!owns_runtime && state.frame_tree) {
      auto* root = state.frame_tree->state(0);
      if (root) JS_SetInterruptHandler(state.runtime, interrupt_script, &root->execution);
    }
    state.active = false;
    state.workers.reset();
    if (state.cancelled && !owns_runtime) state.cancelled->store(true);
    state.frame_owner.reset();
    state.frame_tree = nullptr;
    if (state.runtime && owns_runtime)
      JS_SetHostPromiseRejectionTracker(state.runtime, nullptr, nullptr);
    JS_FreeValue(state.context, state.frame_message_delivery);
    state.frame_message_delivery = JS_UNDEFINED;
    clear_promise_rejections(state);
    if (state.modules) state.modules->clear_compiled(state.context);
    for (const auto& module : state.pending_modules) JS_FreeValue(state.context, module.promise);
    for (auto& job : state.network.fetches)
      if (job->cancelled) job->cancelled->store(true);
    state.network.fetches.clear();
    for (auto& item : state.timers) {
      JS_FreeValue(state.context, item.second->callback);
      item.second->callback = JS_UNDEFINED;
      delete item.second;
    }
    state.scheduler.clear();
    state.ports.reset();
    state.broadcasts.reset();
    state.indexed_db.reset();
    if (state.css.selectors) lxb_selectors_destroy(state.css.selectors, true);
    if (state.css.css_parser) {
      lxb_css_parser_selectors_destroy(state.css.css_parser);
      lxb_css_parser_destroy(state.css.css_parser, true);
    }
    shutdown_wasm(state);
    if (state.context) JS_FreeContext(state.context);
    if (state.runtime && owns_runtime) {
      JS_RunGC(state.runtime);
      JS_FreeRuntime(state.runtime);
    }
  }
};

JsRuntime::JsRuntime(lxb_html_document_t* document, Page& page, StorageStore& storage,
                     const std::string& html, HistoryContext history,
                     std::shared_ptr<std::atomic<bool>> cancelled, const FrameEnvironment* frame)
    : impl_(std::make_unique<Impl>()) {
  LoadTrace trace("runtime");
  auto& s = impl_->state;
  s.cancelled = std::move(cancelled);
  s.execution.set_cancellation(s.cancelled);
  s.history_archive =
      history.archive ? std::move(history.archive) : std::make_shared<HistoryArchive>();
  s.base_url = page.url;
  s.referrer_policy = page.referrer_policy;
  s.document_referrer = page.document_referrer;
  s.history_source_url = page.url;
  s.security_origin = frame ? frame->origin : url_origin(page.url);
  s.secure_context =
      potentially_trustworthy_url(s.security_origin) &&
      (!frame || frame->tree->parent(frame->id) == frame->id || frame->parent->secure_context);
  if (frame) {
    impl_->owns_runtime = false;
    s.frame_id = frame->id;
    s.frame_tree = frame->tree;
    s.frame_document = frame->document;
    s.opaque_origin = frame->opaque;
    s.scripting_allowed = frame->scripts;
    s.base_url = frame->base;
    s.errors = frame->parent->errors;
  } else {
    s.frame_owner = std::make_shared<FrameTree>(s);
    s.frame_tree = s.frame_owner.get();
  }
  s.dom.document = document;
  s.frame_tree->bind(s);
  s.css.stylesheets =
      load_visibility_stylesheets(lxb_dom_interface_node(&document->dom_document), s.base_url, page,
                                  nullptr, s.referrer_policy, s.cancelled);
  trace.mark("stylesheets");
  s.storage = &storage;
  if (!frame) s.errors = &page.script_errors;
  s.runtime = frame ? frame->parent->runtime : JS_NewRuntime();
  if (!s.runtime) throw CliError("RUNTIME_INIT", "cannot create QuickJS runtime");
  // Frames share their parent's runtime and its configured allocation ceiling.
  // The default remains engine sizing; this is not a total process-memory bound.
  if (!frame && runtime_policy().js_heap_bytes)
    JS_SetMemoryLimit(s.runtime, runtime_policy().js_heap_bytes);
  // The Windows executable reserves 8 MiB, matching the usual Unix main stack.
  // Keep native headroom while allowing ordinary framework tree recursion.
  JS_SetMaxStackSize(s.runtime, kJsStackBytes);
  ExecutionBudget::Window load_window(s.execution);
  s.execution.begin_task();
  JS_SetInterruptHandler(s.runtime, interrupt_script, &s.execution);
  s.context = JS_NewContext(s.runtime);
  if (!s.context) throw CliError("RUNTIME_INIT", "cannot create QuickJS context");
  if (JS_AddIntrinsicDOMException(s.context) < 0)
    throw CliError("RUNTIME_INIT", "cannot initialize DOMException");
  s.modules = std::make_unique<ModuleLoader>(
      s.runtime, s.base_url, s.opaque_origin ? "null" : s.security_origin, &s.execution,
      &s.script_loading, &s.referrer_policy, s.cancelled);
  s.css.css_parser = lxb_css_parser_create();
  s.css.selectors = lxb_selectors_create();
  if (!s.css.css_parser || !s.css.selectors ||
      lxb_css_parser_init(s.css.css_parser, nullptr) != LXB_STATUS_OK ||
      lxb_css_parser_selectors_init(s.css.css_parser) != LXB_STATUS_OK ||
      lxb_selectors_init(s.css.selectors) != LXB_STATUS_OK)
    throw CliError("RUNTIME_INIT", "cannot initialize Lexbor selectors");
  account_initial_dom(s);
  dom_node_id(&s, lxb_dom_interface_node(&document->dom_document));
  JS_SetContextOpaque(s.context, &s);
  JS_SetHostPromiseRejectionTracker(s.runtime, promise_rejection_tracker, nullptr);
  retain_frame_realm(s, impl_->owned_state);
  trace.mark("realm-retention");
  const JsValueOwner global_owner(s.context, JS_GetGlobalObject(s.context));
  const auto global = global_owner.get();
  install_native_bindings(s.context, global);
  JS_SetPropertyStr(s.context, global, "__webcliCurrentURL",
                    JS_NewString(s.context, page.url.c_str()));
  JS_SetPropertyStr(s.context, global, "__webcliInheritedBase",
                    JS_NewString(s.context, s.base_url.c_str()));
  JS_SetPropertyStr(s.context, global, "__webcliUserAgent",
                    JS_NewString(s.context, kUserAgent.c_str()));
  JS_SetPropertyStr(s.context, global, "__webcliBrowserBrand",
                    JS_NewString(s.context, kBrowserBrand));
  JS_SetPropertyStr(s.context, global, "__webcliBrowserMajorVersion",
                    JS_NewString(s.context, kBrowserMajorVersion));
  JS_SetPropertyStr(s.context, global, "__webcliBrowserVersion",
                    JS_NewString(s.context, kBrowserVersion));
  JS_SetPropertyStr(s.context, global, "__webcliArchitecture",
                    JS_NewString(s.context, kBrowserArchitecture));
  JS_SetPropertyStr(s.context, global, "__webcliBitness", JS_NewString(s.context, kBrowserBitness));
  JS_SetPropertyStr(s.context, global, "__webcliSecureContext",
                    JS_NewBool(s.context, s.secure_context));
#if defined(_WIN32)
  constexpr const char* platform = "Win32";
#elif defined(__APPLE__)
  constexpr const char* platform = "MacIntel";
#elif defined(__linux__) && defined(__aarch64__)
  constexpr const char* platform = "Linux aarch64";
#elif defined(__linux__) && defined(__x86_64__)
  constexpr const char* platform = "Linux x86_64";
#else
  constexpr const char* platform = "Unknown";
#endif
  JS_SetPropertyStr(s.context, global, "__webcliPlatform", JS_NewString(s.context, platform));
  JS_SetPropertyStr(s.context, global, "__webcliHistoryLength",
                    JS_NewInt64(s.context, history.length));
  JS_SetPropertyStr(s.context, global, "__webcliHistoryOffset",
                    JS_NewInt64(s.context, history.offset));
  JS_SetPropertyStr(s.context, global, "__webcliHistoryInitialState",
                    JS_NewInt32(s.context, history.state_id));
  if (!evaluate("const __webcliLimits=Object.freeze({bufferedBodyBytes:" +
                    std::to_string(kBufferedBodyBytes) +
                    ",authorScripts:" + std::string(s.scripting_allowed ? "true" : "false") +
                    ",timerDelayMs:" + std::to_string(kTimerDelayMs) + "});" +
                    "const __webcliViewport=Object.freeze({width:" +
                    std::to_string(kDesktopViewportWidth) +
                    ",height:" + std::to_string(kDesktopViewportHeight) + "});",
                "webcli:limits"))
    throw CliError(
        "RUNTIME_INIT",
        "cannot initialize runtime limits: " +
            (s.errors && !s.errors->empty() ? s.errors->back() : std::string("unknown error")));
  trace.mark("limits");
  if (!evaluate(kWebApiSource, "webcli:web-api"))
    throw CliError(
        "RUNTIME_INIT",
        "WebCLI Web API initialization failed: " +
            (s.errors && !s.errors->empty() ? s.errors->back() : std::string("unknown error")));
  trace.mark("bootstrap");
  if (frame && JS_SetPropertyStr(s.context, global, "name",
      JS_NewString(s.context, frame->name.c_str())) < 0)
    throw std::bad_alloc();
  s.frame_message_delivery = JS_GetPropertyStr(s.context, global, "__webcliDeliverFrameMessage");
  parse_live_document(*this, s, page, html);
  s.frame_tree->scan(s);
  if (frame) JS_SetInterruptHandler(s.runtime, interrupt_script, &frame->parent->execution);
}
JsRuntime::~JsRuntime() = default;
ScriptContext& JsRuntime::script_context() { return impl_->state; }
std::uint64_t JsRuntime::observation_revision() const {
  impl_->state.require_owner();
  return impl_->state.css.style_generation;
}
const VisibilityValues& JsRuntime::visibility_values(bool& truncated) {
  impl_->state.require_owner();
  const auto& values = resolve_visibility(impl_->state);
  truncated = truncated || impl_->state.css.style_truncated;
  return values;
}

const std::string* JsRuntime::stylesheet_source(lxb_dom_node_t* node) const {
  impl_->state.require_owner();
  const auto inline_sheet = impl_->state.css.inline_stylesheets.find(node);
  if (inline_sheet != impl_->state.css.inline_stylesheets.end()) {
    if (inline_sheet->second.original == node_text(node)) return &inline_sheet->second.source;
    impl_->state.css.inline_stylesheet_bytes -=
        inline_sheet->second.original.size() + inline_sheet->second.source.size();
    impl_->state.css.inline_stylesheets.erase(inline_sheet);
  }
  const auto found = impl_->state.css.stylesheets.find(node);
  return found != impl_->state.css.stylesheets.end() && found->second.href == attr(node, "href") &&
                 !has_attr(node, "disabled")
             ? found->second.source.get()
             : nullptr;
}
bool JsRuntime::evaluate(const std::string& source, const std::string& origin) {
  impl_->state.require_owner();
  auto& s = impl_->state;
  JS_SetInterruptHandler(s.runtime, interrupt_script, &s.execution);
  s.modules->begin_turn();
  s.error_context = origin;
  ExecutionBudget::Window operation_window(s.execution);
  if (!begin_js_task(&s)) return false;
  auto value =
      JS_Eval(s.context, source.data(), source.size(), origin.c_str(), JS_EVAL_TYPE_GLOBAL);
  const bool ok = !JS_IsException(value);
  if (!ok) record_async_js_error(&s, s.context);
  JS_FreeValue(s.context, value);
  return ok;
}
void JsRuntime::checkpoint() {
  impl_->state.require_owner();
  auto& s = impl_->state;
  if (!s.active) return;
  JS_SetInterruptHandler(s.runtime, interrupt_script, &s.execution);
  ExecutionBudget::Window operation_window(s.execution);
  if (!begin_js_task(&s)) return;
  drain_timer_jobs(&s);
  if (!JS_IsJobPending(s.runtime)) flush_promise_rejections(s);
}

void JsRuntime::pump() {
  impl_->state.require_owner();
  auto& s = impl_->state;
  if (!s.active) return;
  JS_SetInterruptHandler(s.runtime, interrupt_script, &s.execution);
  if (s.frame_id == 0) s.frame_tree->pump();
  s.modules->begin_turn();
  ExecutionBudget::Window operation_window(s.execution);
  if (!begin_js_task(&s)) return;
  s.error_context = "asynchronous page task";
  if (s.workers) s.workers->pump();
  if (s.broadcasts) s.broadcasts->pump();
  // Finish the previous JavaScript task's microtasks before timer callbacks.
  drain_timer_jobs(&s);
  if (s.execution.window_expired() || JS_IsJobPending(s.runtime)) return;
  s.css.stylesheet_resources.poll(s);
  start_queued_http(s);
  for (auto it = s.network.fetches.begin(); it != s.network.fetches.end();) {
    auto& job = **it;
    if (s.execution.window_expired()) {
      begin_js_task(&s);
      break;
    }
    if (!job.future.valid() ||
        job.future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
      ++it;
      continue;
    }
    auto completed = std::move(*it);
    it = s.network.fetches.erase(it);
    schedule_resource_completion(s, std::move(completed));
  }
  const auto slice_end = TaskScheduler::Clock::now() + std::chrono::milliseconds(8);
  while (s.scheduler.has_ready() && TaskScheduler::Clock::now() < slice_end) {
    if (!begin_js_task(&s)) return;
    s.scheduler.run_one();
    drain_timer_jobs(&s);
    if (JS_IsJobPending(s.runtime)) return;
    // Commit history actions before advancing to another scheduled task.
    if (!s.history_actions.empty()) break;
  }
  drain_timer_jobs(&s);
  start_queued_http(s);
  for (auto it = s.pending_modules.begin(); it != s.pending_modules.end();) {
    const auto status = JS_PromiseState(s.context, it->promise);
    if (status == JS_PROMISE_PENDING) {
      ++it;
      continue;
    }
    JS_FreeValue(s.context, it->promise);
    it = s.pending_modules.erase(it);
  }
  if (!JS_IsJobPending(s.runtime)) flush_promise_rejections(s);
  advance_document_lifecycle(*this, s);
  s.frame_tree->scan(s);
}
void JsRuntime::suspend() {
  impl_->state.require_owner();
  auto& s = impl_->state;
  if (!s.active) return;
  s.active = false;
  s.scheduler.pause();
}
void JsRuntime::resume() {
  impl_->state.require_owner();
  auto& s = impl_->state;
  if (s.active) return;
  s.active = true;
  s.scheduler.resume();
}
bool JsRuntime::network_pending() const {
  impl_->state.require_owner();
  return !impl_->state.network.fetches.empty() || impl_->state.css.stylesheet_resources.pending() ||
         (impl_->state.workers && impl_->state.workers->pending()) ||
         (impl_->state.broadcasts && impl_->state.broadcasts->pending()) ||
         impl_->state.modules->has_pending_sources() ||
         (impl_->state.frame_id == 0 && impl_->state.frame_tree->pending());
}
std::pair<bool, std::string> JsRuntime::select_value(lxb_dom_node_t* node) {
  impl_->state.require_owner();
  return selected_control_value(impl_->state, node);
}
std::string JsRuntime::textarea_value(lxb_dom_node_t* node) {
  impl_->state.require_owner();
  return textarea_control_value(impl_->state, node);
}
int JsRuntime::node_id(lxb_dom_node_t* node) {
  impl_->state.require_owner();
  return dom_node_id(&impl_->state, node);
}
lxb_dom_node_t* JsRuntime::node(int id) const {
  impl_->state.require_owner();
  const auto& nodes = impl_->state.dom.nodes;
  return id >= 0 && static_cast<std::size_t>(id) < nodes.size()
             ? nodes[static_cast<std::size_t>(id)]
             : nullptr;
}
void JsRuntime::prepare_composition() {
  impl_->state.require_owner();
  webcli::prepare_composition(impl_->state);
}
std::string JsRuntime::composed_text(lxb_dom_node_t* root) {
  impl_->state.require_owner();
  std::string result;
  std::vector<lxb_dom_node_t*> pending{root};
  std::size_t visited = 0;
  while (!pending.empty() && result.size() < 65536 && ++visited <= 50000) {
    auto* node = pending.back();
    pending.pop_back();
    const auto tag = tag_name(node);
    if (tag == "script" || tag == "style" || tag == "template" || tag == "noscript" || tag == "svg")
      continue;
    if (node->type == LXB_DOM_NODE_TYPE_TEXT)
      result += node_text(node);
    else {
      auto children = composed_children(node);
      for (auto it = children.rbegin(); it != children.rend(); ++it) pending.push_back(*it);
    }
  }
  return truncate(result, 65536);
}
std::vector<lxb_dom_node_t*> JsRuntime::composed_children(lxb_dom_node_t* node) {
  impl_->state.require_owner();
  return webcli::composed_children(impl_->state, node);
}
lxb_dom_node_t* JsRuntime::composed_parent(lxb_dom_node_t* node) {
  impl_->state.require_owner();
  return webcli::composed_parent(impl_->state, node);
}
bool JsRuntime::matches(const std::string& selector) {
  impl_->state.require_owner();
  auto& s = impl_->state;
  JSValue args[] = {JS_NewInt32(s.context, 0), JS_NewString(s.context, selector.c_str())};
  auto result = js_dom_query(s.context, JS_UNDEFINED, 2, args);
  JS_FreeValue(s.context, args[0]);
  JS_FreeValue(s.context, args[1]);
  if (JS_IsException(result)) {
    record_async_js_error(&s, s.context);
    JS_FreeValue(s.context, result);
    throw CliError("INVALID_SELECTOR", "invalid CSS selector");
  }
  auto length = JS_GetPropertyStr(s.context, result, "length");
  uint32_t count = 0;
  JS_ToUint32(s.context, &count, length);
  JS_FreeValue(s.context, length);
  JS_FreeValue(s.context, result);
  return count != 0;
}
void JsRuntime::sync(Page& page) {
  impl_->state.require_owner();
  auto& s = impl_->state;
  auto global = JS_GetGlobalObject(s.context);
  const auto candidate = property_string(s.context, global, "__webcliCurrentURL", page.url);
  if (same_origin(candidate, s.history_source_url)) page.url = candidate;
  JS_FreeValue(s.context, global);
  if (page.url != "about:blank" && page.url != "about:srcdoc") s.base_url = page.url;
  page.referrer_policy = s.referrer_policy;
  page.diagnostic_trace.assign(s.diagnostic_trace.begin(), s.diagnostic_trace.end());
  page.diagnostic_dropped = s.diagnostic_dropped;
  page.pending_requests = s.network.fetches.size();
  page.pending_timers = s.timers.size();
  page.pending_modules = s.pending_modules.size();
  page.pending_microtasks = JS_IsJobPending(s.runtime);
}
std::string JsRuntime::document_base_url() const {
  impl_->state.require_owner();
  const auto& state = impl_->state;
  const JsValueOwner global(state.context, JS_GetGlobalObject(state.context));
  const JsValueOwner document(state.context,
                              JS_GetPropertyStr(state.context, global.get(), "document"));
  return property_string(state.context, document.get(), "baseURI", state.base_url);
}
std::optional<NavigationAction> JsRuntime::take_navigation() {
  impl_->state.require_owner();
  auto& s = impl_->state;
  auto action = std::move(s.navigation);
  s.navigation.reset();
  return action;
}
std::pair<int, int> JsRuntime::take_submission() {
  impl_->state.require_owner();
  auto& s = impl_->state;
  auto global = JS_GetGlobalObject(s.context);
  auto value = JS_GetPropertyStr(s.context, global, "__webcliSubmission");
  std::pair<int, int> result{-1, -1};
  if (JS_IsObject(value)) {
    auto f = JS_GetPropertyStr(s.context, value, "form"),
         b = JS_GetPropertyStr(s.context, value, "button");
    JS_ToInt32(s.context, &result.first, f);
    JS_ToInt32(s.context, &result.second, b);
    JS_FreeValue(s.context, f);
    JS_FreeValue(s.context, b);
  }
  JS_FreeValue(s.context, value);
  JS_SetPropertyStr(s.context, global, "__webcliSubmission", JS_NULL);
  JS_FreeValue(s.context, global);
  return result;
}
std::vector<HistoryAction> JsRuntime::take_history_actions() {
  impl_->state.require_owner();
  return std::exchange(impl_->state.history_actions, {});
}
void JsRuntime::retain_history(std::size_t length, std::size_t offset) {
  impl_->state.require_owner();
  std::size_t pushes = 0;
  // Include pushes queued by parsing or popstate handlers but not yet committed.
  for (const auto& action : impl_->state.history_actions)
    if (action.kind != HistoryAction::Kind::Traverse) {
      if (action.kind == HistoryAction::Kind::Push) ++pushes;
    }
  evaluate("__historyEngine.retain(" + std::to_string(length) + "," + std::to_string(offset) + "," +
               std::to_string(pushes) + ")",
           "history retention");
}
void JsRuntime::restore_history(const std::string& url, int index, std::size_t length,
                                std::size_t offset, bool dispatch_pop) {
  impl_->state.require_owner();
  evaluate("__historyEngine.restore(" + json_escape(url) + "," + std::to_string(index) + "," +
               std::to_string(length) + "," + std::to_string(offset) + "," +
               (dispatch_pop ? "true" : "false") + ")",
           "history traversal");
  checkpoint();
}
void JsRuntime::notify_history_traversal() {
  impl_->state.require_owner();
  evaluate("__historyEngine.notify()", "history traversal");
  checkpoint();
}
}  // namespace webcli
