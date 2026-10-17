#include "runtime/css/stylesheet_resources.hpp"

#include <algorithm>
#include <sstream>
#include <unordered_set>
#include <vector>

#include "network/referrer.hpp"
#include "network/resource_service.hpp"
#include "runtime/css/visibility_rules.hpp"
#include "runtime/dom/dom_bindings.hpp"
#include "runtime/dom/dom_helpers.hpp"
#include "runtime/engine/diagnostics.hpp"
#include "runtime/engine/js_handles.hpp"
#include "runtime/engine/script_context.hpp"
#include "runtime/network/referrer_policy.hpp"

namespace webcli {
namespace {
bool style_resource_link(lxb_dom_node_t* node, bool& applied) {
  if (tag_name(node) != "link" || has_attr(node, "disabled")) return false;
  std::istringstream rel(attr(node, "rel"));
  bool stylesheet = false, alternate = false, preload = false;
  for (std::string token; rel >> token;) {
    std::transform(token.begin(), token.end(), token.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    stylesheet |= token == "stylesheet";
    alternate |= token == "alternate";
    preload |= token == "preload";
  }
  applied = stylesheet && !alternate;
  auto as = attr(node, "as");
  std::transform(as.begin(), as.end(), as.begin(),
                 [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
  return (applied || (preload && as == "style")) && !attr(node, "href").empty();
}
void warning(ScriptContext& state, const std::string& text) {
  if (state.errors->size() < kMaxScriptErrors) state.errors->push_back("stylesheet: " + text);
}
}  // namespace
StylesheetResources::~StylesheetResources() {
  for (const auto& item : resources_)
    if (item.second->cancelled) item.second->cancelled->store(true);
}
void StylesheetResources::scan(ScriptContext& state) {
  if (scanned_revision_ == state.css.style_generation) return;
  std::unordered_set<lxb_dom_node_t*> seen;
  std::vector<lxb_dom_node_t*> nodes{lxb_dom_interface_node(&state.dom.document->dom_document)};
  for (const auto& root : state.dom.shadow_roots) {
    auto* ancestor = root.first;
    while (ancestor && ancestor != lxb_dom_interface_node(&state.dom.document->dom_document)) {
      const auto host = state.dom.shadow_hosts.find(ancestor);
      ancestor = host == state.dom.shadow_hosts.end() ? ancestor->parent : host->second;
    }
    if (ancestor) nodes.push_back(root.second);
  }
  while (!nodes.empty()) {
    auto* node = nodes.back();
    nodes.pop_back();
    for (auto* child = node->last_child; child; child = child->prev) nodes.push_back(child);
    bool applied = false;
    if (!style_resource_link(node, applied)) continue;
    const auto href = attr(node, "href");
    seen.insert(node);
    const auto existing = attachments_.find(node);
    if (existing != attachments_.end() && existing->second.href == href &&
        existing->second.stylesheet == applied) continue;
    const auto url = resolve_web_url(state.base_url, href);
    const auto policy = element_referrer_policy(state.referrer_policy, node);
    const auto key = url ? *url + '\n' + request_referrer(state.base_url, *url, policy) : "";
    auto resource = std::make_shared<Resource>();
    if (url && resources_.count(key))
      resource = resources_.at(key);
    else {
      const auto cached = state.css.stylesheets.find(node);
      if (cached != state.css.stylesheets.end() && cached->second.href == href) {
        resource->source = cached->second.source;
        resource->complete = true;
        if (resource->source) retained_bytes_ += resource->source->size();
      } else if (!url || resources_.size() >= kStylesheetRequests) {
        resource->complete = true;
        resource->error =
            !url ? "invalid stylesheet URL: " + href : "stylesheet request quota exceeded: " + href;
        warning(state, resource->error);
      } else {
        HttpRequest request;
        request.metadata_mode = "no-cors";
        request.metadata_origin = state.opaque_origin ? "null" : state.security_origin;
        request.destination = "style";
        request.owner_cancelled = state.cancelled;
        request.url = *url;
        request.cookie_origin = state.base_url;
        request.referrer_source = state.base_url;
        request.referrer_policy = policy;
        request.headers["Accept"] = "text/css,*/*;q=0.1";
        request.max_body_bytes = kStylesheetBodyBytes;
        request.timeout_ms = kStylesheetLoadTimeoutMs;
        request.cancelled = resource->cancelled = std::make_shared<std::atomic<bool>>(false);
        try {
          resource->transfer = resource_service().submit(std::move(request));
        } catch (const std::exception& error) {
          resource->complete = true;
          resource->error = error.what();
          warning(state, resource->error);
        }
      }
      if (url) resources_.emplace(key, resource);
    }
    attachments_.insert_or_assign(node, Attachment{href, resource, false, applied});
    if (applied)
      state.css.stylesheets.insert_or_assign(node, LoadedStylesheet{href, resource->source});
    else
      state.css.stylesheets.erase(node);
    ++state.css.style_generation;
  }
  for (auto it = attachments_.begin(); it != attachments_.end();) {
    if (seen.count(it->first)) {
      ++it;
      continue;
    }
    state.css.stylesheets.erase(it->first);
    it = attachments_.erase(it);
    ++state.css.style_generation;
  }
  scanned_revision_ = state.css.style_generation;
}
void StylesheetResources::queue_event(ScriptContext& state, lxb_dom_node_t* node,
                                      const Attachment& attachment) {
  const auto resource = attachment.resource;
  const auto href = attachment.href;
  const auto applied = attachment.stylesheet;
  const auto id = dom_node_id(&state, node);
  state.scheduler.schedule(
      TaskScheduler::Source::Resource, TaskScheduler::Clock::duration::zero(),
      [this, &state, resource, href, id, applied] {
        --notifications_;
        // Adoption can migrate a node's native storage; collection can release
        // it entirely. Resolve its current ID before touching DOM memory.
        auto* node = id >= 0 && static_cast<std::size_t>(id) < state.dom.nodes.size()
                         ? state.dom.nodes[static_cast<std::size_t>(id)] : nullptr;
        if (!node) return;
        const auto current = attachments_.find(node);
        bool now_applied = false;
        if (current == attachments_.end() || current->second.resource != resource ||
            current->second.stylesheet != applied || attr(node, "href") != href ||
            !style_resource_link(node, now_applied) || now_applied != applied)
          return;
        const auto type = resource->source ? "load" : "error";
        const auto code = "globalThis.__webcliScheduleIntersectionChecks?.();"
                          "__dispatchInputEvent(__wrap(" +
                          std::to_string(id) + "),new Event('" + type + "'));";
        JsValueOwner result(state.context,
                            JS_Eval(state.context, code.data(), code.size(),
                                    "stylesheet resource event", JS_EVAL_TYPE_GLOBAL));
        if (JS_IsException(result.get())) record_async_js_error(&state, state.context);
      });
  ++notifications_;
}
void StylesheetResources::poll(ScriptContext& state) {
  scan(state);
  for (const auto& item : resources_) {
    auto& resource = *item.second;
    if (resource.complete || !resource.transfer.valid() ||
        resource.transfer.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
      continue;
    try {
      auto response = resource.transfer.get();
      if (response.status < 200 || response.status >= 300)
        throw CliError("CSS_HTTP_STATUS", "HTTP " + std::to_string(response.status));
      if (response.content_type.rfind("text/css", 0) != 0)
        throw CliError("CSS_CONTENT_TYPE", "expected text/css");
      bool truncated = false;
      auto text = visibility_css_source(response.body, truncated);
      if (text.size() >
          kVisibilityStylesheetBytes - std::min(kVisibilityStylesheetBytes, retained_bytes_))
        throw CliError("CSS_QUOTA", "stylesheet sources exceed 2 MiB");
      resource.source = std::make_shared<const std::string>(std::move(text));
      retained_bytes_ += resource.source->size();
      if (truncated) warning(state, item.first + ": rule quota exceeded");
    } catch (const std::exception& error) {
      resource.error = item.first + ": " + error.what();
      warning(state, resource.error);
    }
    resource.complete = true;
  }
  for (auto& item : attachments_) {
    auto& attachment = item.second;
    if (!attachment.resource->complete || attachment.notified) continue;
    if (attachment.stylesheet)
      state.css.stylesheets.insert_or_assign(
          item.first, LoadedStylesheet{attachment.href, attachment.resource->source});
    ++state.css.style_generation;
    queue_event(state, item.first, attachment);
    attachment.notified = true;
  }
}
bool StylesheetResources::pending() const {
  if (notifications_) return true;
  return std::any_of(attachments_.begin(), attachments_.end(), [](const auto& item) {
    return !item.second.resource->complete || !item.second.notified;
  });
}
}  // namespace webcli
