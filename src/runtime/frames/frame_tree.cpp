#include "runtime/frames/frame_tree.hpp"

#include <algorithm>
#include <future>
#include <map>
#include <sstream>
#include <tuple>
#include <vector>

#include "core/errors.hpp"
#include "core/load_trace.hpp"
#include "core/text.hpp"
#include "core/url/url.hpp"
#include "network/http_client.hpp"
#include "network/referrer.hpp"
#include "network/resource_service.hpp"
#include "runtime/dom/document_loader.hpp"
#include "runtime/dom/dom_bindings.hpp"
#include "runtime/dom/dom_helpers.hpp"
#include "runtime/engine/diagnostics.hpp"
#include "runtime/engine/js_handles.hpp"
#include "runtime/engine/js_runtime.hpp"
#include "runtime/engine/script_context.hpp"
#include "runtime/forms/form_submission.hpp"
#include "runtime/frames/frame_policy.hpp"
#include "runtime/network/referrer_policy.hpp"
#include "runtime/workers/worker_manager.hpp"

namespace webcli {
namespace {
std::string ascii_lower(std::string value) {
  for (auto& character : value)
    if (character >= 'A' && character <= 'Z') character += 'a' - 'A';
  return value;
}
bool connected(ScriptContext& owner, lxb_dom_node_t* node) {
  for (; node; node = node->parent)
    if (node == lxb_dom_interface_node(&owner.dom.document->dom_document)) return true;
  return false;
}
bool token(const std::string& value, const char* wanted) {
  std::istringstream stream(value);
  for (std::string item; stream >> item;)
    if (ascii_lower(item) == wanted) return true;
  return false;
}
std::shared_ptr<lxb_html_document_t> new_document() {
  auto* document = lxb_html_document_create();
  if (!document) throw std::bad_alloc();
  return {document, [](lxb_html_document_t* value) { lxb_html_document_destroy(value); }};
}
}  // namespace
struct FrameTree::Impl {
  struct Frame {
    int id = 0, parent = 0;
    lxb_dom_node_t* element = nullptr;
    ScriptContext* state = nullptr;
    Page page;
    std::uint64_t generation = 0;
    std::unique_ptr<JsRuntime> runtime;
    std::shared_ptr<std::atomic<bool>> transfer_cancel;
    std::future<HttpResponse> transfer;
    std::unique_ptr<DocumentLifecycle::LoadBlocker> blocker;
    std::string signature, destination;
    bool dirty = true, initial = true, loading = false, scripts = true, opaque = false;
    bool popup = false, closed = false, no_referrer = false;
    int opener = -1;
    std::string name;
    std::string initial_origin, initial_base;
  };
  ScriptContext& root;
  FrameTree& tree;
  std::map<int, std::unique_ptr<Frame>> frames;
  std::map<std::pair<int, int>, int> nodes;
  std::map<int, std::uint64_t> scanned;
  int next_id = 0;
  std::uint64_t next_generation = 0;
  using Observation = std::tuple<int, std::uint64_t, std::uint64_t, bool>;
  std::vector<Observation> observed_frames;
  std::uint64_t observation_epoch = 0;
  explicit Impl(ScriptContext& value, FrameTree& owner) : root(value), tree(owner) {}
  ScriptContext* state(int id) const {
    if (id == 0) return &root;
    const auto entry = frames.find(id);
    return entry == frames.end() || entry->second->closed ? nullptr : entry->second->state;
  }
  std::vector<int> children(int id) const {
    auto* owner = state(id);
    if (!owner) return {};
    std::map<lxb_dom_node_t*, int> containers;
    for (const auto& entry : frames)
      if (entry.second->parent == id) containers.emplace(entry.second->element, entry.first);
    std::vector<int> result;
    std::vector<lxb_dom_node_t*> pending{
        lxb_dom_interface_node(&owner->dom.document->dom_document)};
    while (!pending.empty()) {
      auto* node = pending.back();
      pending.pop_back();
      if (const auto frame = containers.find(node); frame != containers.end())
        result.push_back(frame->second);
      for (auto* child = node->last_child; child; child = child->prev) pending.push_back(child);
    }
    return result;
  }
  void remove(int id) {
    std::vector<int> descendants;
    for (const auto& entry : frames)
      if (entry.second->parent == id) descendants.push_back(entry.first);
    for (int child : descendants) remove(child);
    const auto found = frames.find(id);
    if (found == frames.end()) return;
    auto& frame = *found->second;
    if (frame.transfer_cancel) frame.transfer_cancel->store(true);
    for (auto it = nodes.begin(); it != nodes.end();) {
      if (it->second == id)
        it = nodes.erase(it);
      else
        ++it;
    }
    frame.runtime.reset();
    scanned.erase(id);
    frames.erase(found);
  }
  void commit(Frame& frame, std::string url, std::string html,
              const HttpResponse* response = nullptr) {
    LoadTrace trace("frame");
    auto* owner = state(frame.parent);
    if (!owner) return;
    std::vector<int> descendants;
    for (const auto& entry : frames)
      if (entry.second->parent == frame.id) descendants.push_back(entry.first);
    for (int child : descendants) remove(child);
    frame.runtime.reset();
    trace.mark("retire-document");
    frame.state = nullptr;
    frame.page = {};
    frame.generation = ++next_generation;
    frame.page.url = url;
    frame.page.document_referrer = response ? response->request_referrer
        : frame.no_referrer ? ""
        : request_referrer(frame.popup ? frame.initial_base : owner->base_url, url,
                           owner->referrer_policy);
    frame.page.referrer_policy =
        frame.no_referrer ? "no-referrer"
                          : element_referrer_policy(owner->referrer_policy, frame.element);
    if (response) {
      frame.page.referrer_policy.clear();
      if (const auto policy = response->headers.find("referrer-policy");
          policy != response->headers.end())
        frame.page.referrer_policy = response_referrer_policy(policy->second, "");
    }
    const auto document = new_document();
    trace.mark("allocate-document");
    lxb_html_document_scripting_set(document.get(), frame.scripts);
    const bool inherited = url == "about:blank" || url == "about:srcdoc";
    FrameEnvironment environment{owner,
                                 &tree,
                                 frame.id,
                                 document,
                                 frame.opaque ? "null"
                                 : inherited  ? (frame.popup ? frame.initial_origin : owner->security_origin)
                                              : url_origin(url),
                                 inherited ? (frame.popup ? frame.initial_base : owner->base_url) : url,
                                 frame.opaque || (inherited && owner->opaque_origin),
                                 frame.scripts, frame.name};
    // No-script sandbox documents still get DOM bindings, but author scripts
    // are made inert before parsing by the parser's explicit scripting flag.
    frame.runtime = std::make_unique<JsRuntime>(
        document.get(), frame.page, *owner->storage, html, HistoryContext{},
        std::make_shared<std::atomic<bool>>(false), &environment);
    trace.mark("create-realm");
    frame.loading = true;
  }
  void event(Frame& frame) {
    if (frame.popup) {
      frame.loading = false;
      return;
    }
    auto* owner = state(frame.parent);
    if (!owner) return;
    const int node = dom_node_id(owner, frame.element);
    frame.blocker.reset();
    frame.loading = false;
    owner->scheduler.schedule(TaskScheduler::Source::Resource, {}, [owner, node] {
      const std::string code =
          "__wrap(" + std::to_string(node) + ").dispatchEvent(new Event('load'));";
      if (!owner->active || !begin_js_task(owner)) return;
      auto value =
          JS_Eval(owner->context, code.data(), code.size(), "iframe load", JS_EVAL_TYPE_GLOBAL);
      if (JS_IsException(value)) record_async_js_error(owner, owner->context);
      JS_FreeValue(owner->context, value);
    });
  }
};
FrameTree::FrameTree(ScriptContext& root) : impl_(std::make_unique<Impl>(root, *this)) {}
FrameTree::~FrameTree() {
  while (!impl_->frames.empty()) impl_->remove(impl_->frames.begin()->first);
}
void FrameTree::bind(ScriptContext& state) {
  impl_->scanned.erase(state.frame_id);
  if (!state.frame_id) return;
  const auto entry = impl_->frames.find(state.frame_id);
  if (entry != impl_->frames.end()) entry->second->state = &state;
}
ScriptContext* FrameTree::state(int id) const { return impl_->state(id); }
JsRuntime* FrameTree::runtime(int id) const {
  const auto entry = impl_->frames.find(id);
  return entry == impl_->frames.end() ? nullptr : entry->second->runtime.get();
}
std::uint64_t FrameTree::generation(int id) const {
  const auto entry = impl_->frames.find(id);
  return entry == impl_->frames.end() ? 0 : entry->second->generation;
}
std::uint64_t FrameTree::observation_revision() const {
  // Compare exact frame identities and DOM versions, rather than hashing them:
  // simultaneous frame updates must never cancel another frame's invalidation.
  bool changed = impl_->observed_frames.size() != impl_->frames.size();
  std::size_t index = 0;
  for (const auto& entry : impl_->frames) {
    const auto& frame = *entry.second;
    const Impl::Observation value{entry.first, frame.generation,
                                  frame.runtime ? frame.runtime->observation_revision() : 0,
                                  frame.closed};
    if (index >= impl_->observed_frames.size()) {
      impl_->observed_frames.push_back(value);
      changed = true;
    } else if (impl_->observed_frames[index] != value) {
      impl_->observed_frames[index] = value;
      changed = true;
    }
    ++index;
  }
  impl_->observed_frames.resize(index);
  if (changed) ++impl_->observation_epoch;
  return impl_->observation_epoch;
}
int FrameTree::parent(int id) const {
  const auto entry = impl_->frames.find(id);
  return entry == impl_->frames.end() ? 0 : entry->second->popup ? id : entry->second->parent;
}
int FrameTree::top(int id) const {
  while (id && parent(id) != id) id = parent(id);
  return id;
}
int FrameTree::opener(int id) const {
  const auto entry = impl_->frames.find(id);
  if (entry == impl_->frames.end() || !state(entry->second->opener)) return -1;
  return entry->second->opener;
}
std::vector<int> FrameTree::popups() const {
  std::vector<int> ids;
  for (const auto& [id, frame] : impl_->frames)
    if (frame->popup && !frame->closed) ids.push_back(id);
  return ids;
}
int FrameTree::open_popup(ScriptContext& caller, const std::string& url, const std::string& name,
                          bool no_opener, bool no_referrer) {
  if (!caller.scripting_allowed || caller.opaque_origin)
    throw CliError("POPUP_BLOCKED", "sandboxed documents cannot open auxiliary windows");
  // Preserve containment until propagation of popup sandbox flags is supported.
  for (int ancestor = caller.frame_id; ancestor; ancestor = parent(ancestor)) {
    if (has_attr(element(ancestor), "sandbox"))
      throw CliError("POPUP_BLOCKED", "sandboxed documents cannot open auxiliary windows");
    if (parent(ancestor) == ancestor) break;
  }
  const NavigationInitiator initiator{caller.opaque_origin ? "null" : caller.security_origin,
                                      {caller.base_url, caller.referrer_policy}};
  if (!no_opener && !name.empty() && ascii_lower(name) != "_blank")
    for (const auto& [id, frame] : impl_->frames)
      if (frame->popup && !frame->closed && frame->name == name &&
          frame->opener == caller.frame_id) {
        if (!url.empty()) navigate(id, url, "GET", "", false, &initiator);
        return id;
      }
  auto frame = std::make_unique<Impl::Frame>();
  frame->id = ++impl_->next_id;
  // Auxiliary contexts belong to the page lifetime but aren't child frames.
  frame->parent = 0;
  frame->popup = true;
  frame->initial_origin = caller.security_origin;
  frame->initial_base = caller.base_url;
  frame->no_referrer = no_referrer;
  frame->opener = no_opener ? -1 : caller.frame_id;
  frame->name = ascii_lower(name) == "_blank" ? "" : name;
  frame->dirty = frame->initial = false;
  const int id = frame->id;
  auto* value = frame.get();
  impl_->frames.emplace(id, std::move(frame));
  try {
    impl_->commit(*value, "about:blank", "<!doctype html><html><head></head><body></body></html>");
    value->loading = false;
    if (!url.empty() && url != "about:blank")
      navigate(id, url, "GET", "", false, &initiator);
  } catch (...) {
    impl_->remove(id);
    throw;
  }
  return id;
}
void FrameTree::close_popup(int id) {
  const auto entry = impl_->frames.find(id);
  if (entry == impl_->frames.end() || !entry->second->popup) return;
  entry->second->closed = true;
  if (entry->second->transfer_cancel) entry->second->transfer_cancel->store(true);
  // Defer teardown until the JavaScript callback has returned.
}
lxb_dom_node_t* FrameTree::element(int id) const {
  const auto entry = impl_->frames.find(id);
  return entry == impl_->frames.end() ? nullptr : entry->second->element;
}
int FrameTree::count(int parent_id) const {
  return static_cast<int>(impl_->children(parent_id).size());
}
int FrameTree::child(int parent_id, int index) const {
  const auto children = impl_->children(parent_id);
  return index >= 0 && static_cast<std::size_t>(index) < children.size() ? children[index] : -1;
}
int FrameTree::ensure(ScriptContext& owner, lxb_dom_node_t* node) {
  if (!node || tag_name(node) != "iframe" || !connected(owner, node)) return -1;
  const auto key = std::make_pair(owner.frame_id, dom_node_id(&owner, node));
  const auto found = impl_->nodes.find(key);
  if (found != impl_->nodes.end()) return found->second;
  auto frame = std::make_unique<Impl::Frame>();
  frame->id = ++impl_->next_id;
  frame->parent = owner.frame_id;
  frame->element = node;
  frame->opaque = owner.opaque_origin ||
                  (has_attr(node, "sandbox") && !token(attr(node, "sandbox"), "allow-same-origin"));
  frame->scripts = owner.scripting_allowed &&
                   (!has_attr(node, "sandbox") || token(attr(node, "sandbox"), "allow-scripts"));
  if (ascii_lower(attr(node, "loading")) != "lazy") frame->blocker = owner.lifecycle.block_load();
  const int id = frame->id;
  auto* value = frame.get();
  impl_->frames.emplace(id, std::move(frame));
  impl_->nodes.emplace(key, id);
  trace_runtime(owner, "frame", "attached iframe #" + std::to_string(id));
  try {
    impl_->commit(*value, "about:blank", "<!doctype html><html><head></head><body></body></html>");
    value->loading = false;
  } catch (...) {
    impl_->remove(id);
    throw;
  }
  return id;
}
void FrameTree::cancel_workers() {
  if (impl_->root.workers) impl_->root.workers->cancel_all();
  for (auto& entry : impl_->frames)
    if (entry.second->state && entry.second->state->workers)
      entry.second->state->workers->cancel_all();
}
void FrameTree::changed(ScriptContext& owner, lxb_dom_node_t* node) {
  const int id = ensure(owner, node);
  if (id >= 0) {
    impl_->frames.at(id)->dirty = true;
    impl_->frames.at(id)->initial = false;
  }
}
void FrameTree::scan(ScriptContext& owner) {
  const auto previous = impl_->scanned.find(owner.frame_id);
  if (previous != impl_->scanned.end() && previous->second == owner.css.style_generation) return;
  impl_->scanned[owner.frame_id] = owner.css.style_generation;
  std::vector<lxb_dom_node_t*> pending{lxb_dom_interface_node(&owner.dom.document->dom_document)};
  while (!pending.empty()) {
    auto* node = pending.back();
    pending.pop_back();
    if (tag_name(node) == "iframe") ensure(owner, node);
    for (auto* child = node->last_child; child; child = child->prev) pending.push_back(child);
  }
  // Numeric Window access must exist before author code or a load handler reads
  // window[0], without first requiring a read of window.length/window.frames.
  const JsValueOwner global(owner.context, JS_GetGlobalObject(owner.context));
  const JsValueOwner refresh(
      owner.context, JS_GetPropertyStr(owner.context, global.get(), "__webcliRefreshFrameIndices"));
  if (JS_IsFunction(owner.context, refresh.get())) {
    JSValue size = JS_NewInt32(owner.context, count(owner.frame_id));
    const JsValueOwner result(owner.context,
                              JS_Call(owner.context, refresh.get(), global.get(), 1, &size));
    JS_FreeValue(owner.context, size);
    if (JS_IsException(result.get())) record_async_js_error(&owner, owner.context);
  }
}
void FrameTree::navigate(int id, std::string url, std::string method, std::string body,
                         bool submission, const NavigationInitiator* initiator) {
  const auto found = impl_->frames.find(id);
  if (found == impl_->frames.end()) return;
  auto& frame = *found->second;
  if (frame.transfer_cancel) frame.transfer_cancel->store(true);
  frame.transfer = {};
  auto* owner = state(frame.parent);
  if (!owner) return;
  frame.opaque =
      owner->opaque_origin || (has_attr(frame.element, "sandbox") &&
                               !token(attr(frame.element, "sandbox"), "allow-same-origin"));
  frame.scripts =
      owner->scripting_allowed && (!has_attr(frame.element, "sandbox") ||
                                   token(attr(frame.element, "sandbox"), "allow-scripts"));
  if (submission) {
    for (int ancestor = id; ancestor; ancestor = parent(ancestor)) {
      auto* container = element(ancestor);
      if (has_attr(container, "sandbox") && !token(attr(container, "sandbox"), "allow-forms"))
        throw CliError("FRAME_BLOCKED", "sandbox forbids form submission");
      if (parent(ancestor) == ancestor) break;
    }
  }
  frame.opaque = has_attr(frame.element, "sandbox") &&
                 !token(attr(frame.element, "sandbox"), "allow-same-origin");
  frame.scripts =
      !has_attr(frame.element, "sandbox") || token(attr(frame.element, "sandbox"), "allow-scripts");
  if (submission && has_attr(frame.element, "sandbox") &&
      !token(attr(frame.element, "sandbox"), "allow-forms"))
    throw CliError("FRAME_BLOCKED", "sandbox forbids form submission");
  frame.destination = std::move(url);
  frame.loading = true;
  if (!frame.popup && ascii_lower(attr(frame.element, "loading")) != "lazy" && !frame.blocker)
    frame.blocker = owner->lifecycle.block_load();
  if (frame.destination == "about:srcdoc") {
    impl_->commit(frame, frame.destination, attr(frame.element, "srcdoc"));
    return;
  }
  if (frame.destination == "about:blank") {
    impl_->commit(frame, frame.destination,
                  "<!doctype html><html><head></head><body></body></html>");
    return;
  }
  // Avoid recursive embedding of an ancestor's identical document URL.
  for (int ancestor = frame.parent; !frame.popup; ancestor = parent(ancestor)) {
    auto* context = state(ancestor);
    if (context && context->history_source_url == frame.destination) {
      impl_->event(frame);
      return;
    }
    if (!ancestor || parent(ancestor) == ancestor) break;
  }
  HttpRequest request;
  request.url = frame.destination;
  request.metadata_mode = "navigate";
  const auto* source = (submission || frame.popup) && frame.state ? frame.state : owner;
  request.metadata_origin = initiator ? initiator->origin
      : source->opaque_origin ? "null" : source->security_origin;
  request.destination = frame.popup ? "document" : "iframe";
  request.method = std::move(method);
  request.body = std::move(body);
  request.is_form_submission = submission;
  request.allow_retries = !submission;
  if (request.method == "POST")
    request.headers.emplace("Content-Type", "application/x-www-form-urlencoded");
  request.owner_cancelled = impl_->root.cancelled;
  request.cancelled = frame.transfer_cancel = std::make_shared<std::atomic<bool>>(false);
  request.referrer_source = initiator ? initiator->referrer.source : source->base_url;
  if (frame.no_referrer)
    request.referrer_policy = "no-referrer";
  else if (initiator)
    request.referrer_policy = initiator->referrer.policy;
  else if (submission || frame.popup)
    request.referrer_policy = source->referrer_policy;
  else
    request.referrer_policy = element_referrer_policy(owner->referrer_policy, frame.element);
  request.headers.emplace("Accept",
                          "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");
  frame.transfer = resource_service().submit([request] { return perform_http(request); });
}
void FrameTree::pump() {
  std::vector<int> ids;
  scan(impl_->root);
  for (const auto& entry : impl_->frames) ids.push_back(entry.first);
  for (int id : ids) {
    const auto found = impl_->frames.find(id);
    if (found == impl_->frames.end()) continue;
    auto& frame = *found->second;
    auto* owner = state(frame.parent);
    if (frame.closed || !owner || (!frame.popup && !connected(*owner, frame.element))) {
      impl_->remove(id);
      continue;
    }
    try {
      const std::string signature =
          attr(frame.element, "src") + '\0' +
          (has_attr(frame.element, "srcdoc") ? "1" + attr(frame.element, "srcdoc") : "0");
      if (!frame.popup && (frame.dirty || signature != frame.signature)) {
        frame.signature = signature;
        frame.dirty = false;
        std::string destination = "about:blank";
        if (has_attr(frame.element, "srcdoc"))
          destination = "about:srcdoc";
        else if (!attr(frame.element, "src").empty()) {
          auto resolved = resolve_web_url(owner->base_url, attr(frame.element, "src"));
          if (resolved) destination = *resolved;
        }
        trace_runtime(*owner, "frame", "load iframe #" + std::to_string(id) + " " +
                                           diagnostic_url(destination));
        if (frame.initial && destination == "about:blank")
          frame.loading = true;
        else
          navigate(id, std::move(destination));
        frame.initial = false;
      }
      if (frame.transfer.valid() &&
          frame.transfer.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
        auto response = frame.transfer.get();
        std::vector<std::string> ancestors;
        for (int ancestor = frame.parent; !frame.popup; ancestor = parent(ancestor)) {
          auto* context = state(ancestor);
          ancestors.push_back(context && !context->opaque_origin ? context->security_origin
                                                                 : "null");
          if (!ancestor || parent(ancestor) == ancestor) break;
        }
        if (!frame.popup &&
            !frame_response_allowed(response.headers, url_origin(response.url), ancestors))
          throw CliError("FRAME_BLOCKED", "response forbids embedding");
        if (!is_html_content_type(response.content_type.c_str()) &&
            !is_plain_text_content_type(response.content_type))
          throw CliError("UNSUPPORTED_CONTENT_TYPE", "iframe expected HTML or plain text");
        auto html = is_plain_text_content_type(response.content_type)
                        ? plain_text_document(response.body)
                        : std::move(response.body);
        impl_->commit(frame, response.url, std::move(html), &response);
      }
      if (frame.runtime) {
        frame.runtime->pump();
        for (const auto& action : frame.runtime->take_history_actions()) {
          if (action.kind == HistoryAction::Kind::Traverse)
            throw CliError("UNSUPPORTED_FRAME_HISTORY", "iframe history traversal is unsupported");
        }
        frame.runtime->sync(frame.page);
        if (auto next = frame.runtime->take_navigation())
          navigate(id, next->url, "GET", "", false, &next->initiator);
        else {
          const auto submission = frame.runtime->take_submission();
          if (submission.first >= 0) {
            auto request = prepare_form_navigation(*frame.runtime, submission.first,
                                                   submission.second, frame.page.url);
            navigate(id, std::move(request.url), std::move(request.method), std::move(request.body),
                     true);
          }
        }
        if (frame.loading && frame.state &&
            std::string(frame.state->lifecycle.ready_state()) == "complete" &&
            !frame.transfer.valid())
          impl_->event(frame);
      }
    } catch (const std::exception& error) {
      if (!frame.runtime) frame.state = nullptr;
      if (impl_->root.errors && impl_->root.errors->size() < kMaxScriptErrors)
        impl_->root.errors->push_back("iframe " + frame.destination + ": " + error.what());
      frame.transfer = {};
      impl_->event(frame);
    }
  }
}
bool FrameTree::pending() const {
  for (const auto& entry : impl_->frames)
    if (entry.second->dirty || entry.second->loading || entry.second->transfer.valid() ||
        (entry.second->runtime && entry.second->runtime->network_pending()))
      return true;
  return false;
}
bool FrameTree::matches(const std::string& selector) const {
  for (const auto& entry : impl_->frames)
    if (entry.second->runtime && entry.second->runtime->matches(selector)) return true;
  return false;
}
}  // namespace webcli
