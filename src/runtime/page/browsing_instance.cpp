#include "runtime/page/browsing_instance.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <thread>

#include "core/errors.hpp"
#include "core/load_trace.hpp"
#include "core/serialization/json_string.hpp"
#include "network/http_client.hpp"
#include "network/referrer.hpp"
#include "observation/semantic_extractor.hpp"
#include "runtime/dom/document_loader.hpp"
#include "runtime/dom/dom_helpers.hpp"
#include "runtime/engine/js_runtime.hpp"
#include "runtime/engine/script_context.hpp"
#include "runtime/forms/form_submission.hpp"
#include "runtime/frames/frame_tree.hpp"

namespace webcli {
namespace {
using Clock = std::chrono::steady_clock;
std::atomic<unsigned long long> next_document{0};
}  // namespace
struct BrowsingInstance::Document {
  Page page;
  std::unique_ptr<lxb_html_document_t, decltype(&lxb_html_document_destroy)> dom{
      nullptr, lxb_html_document_destroy};
  SemanticExtractor extractor;
  std::unique_ptr<JsRuntime> js;
  std::size_t logged_errors = 0;
  std::uint64_t observed_revision = 0;
  std::string observed_url;
  const std::uint64_t history_group;
  const std::shared_ptr<const NavigationReferrer> navigation_referrer;
  Document(HttpResponse response, StorageStore& storage, HistoryContext history,
           std::uint64_t group, std::shared_ptr<std::atomic<bool>> cancelled)
      : history_group(group),
        navigation_referrer(std::make_shared<const NavigationReferrer>(
            NavigationReferrer{response.request_referrer, response.request_referrer_policy})) {
    LoadTrace trace("document");
    page.http_status = response.status;
    const auto challenge = response.headers.find("x-amzn-waf-action");
    const auto cloudflare_challenge = response.headers.find("cf-mitigated");
    page.access_challenge =
        (challenge != response.headers.end() && challenge->second == "challenge") ||
        (cloudflare_challenge != response.headers.end() &&
         cloudflare_challenge->second == "challenge");
    page.url = response.url;
    page.document_url = response.url;
    page.document_referrer = response.request_referrer;
    if (const auto policy = response.headers.find("referrer-policy");
        policy != response.headers.end())
      page.referrer_policy = response_referrer_policy(policy->second, "");
    page.document_id = std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) +
                       "-" + std::to_string(++next_document);
    dom.reset(lxb_html_document_create());
    if (!dom) throw CliError("DOM_INIT", "cannot allocate Lexbor document");
    // Live pages execute scripts: noscript must use the scripting-enabled
    // HTML parsing rules, as it does in PageCore's live document constructor.
    lxb_html_document_scripting_set(dom.get(), true);
    auto source = is_plain_text_content_type(response.content_type)
                      ? plain_text_document(response.body)
                      : std::move(response.body);
    js = std::make_unique<JsRuntime>(dom.get(), page, storage, source, std::move(history),
                                     std::move(cancelled));
    trace.mark("runtime");
    extractor.extract(dom.get(), page, *js);
    observed_revision =
        js->observation_revision() ^ js->script_context().frame_tree->observation_revision();
    observed_url = page.url;
    trace.mark("extract");
  }
};
BrowsingInstance::BrowsingInstance(std::shared_ptr<OriginStorage> local,
                                   std::shared_ptr<std::atomic<bool>> cancelled)
    : storage_(std::move(local)), cancelled_(std::move(cancelled)) {}
BrowsingInstance::~BrowsingInstance() = default;
void BrowsingInstance::mark_diagnostics_reported() {
  if (current_) current_->page.human_errors_reported = current_->page.script_errors.size();
}
void BrowsingInstance::navigate(const std::string& url, const std::string& method,
                                const std::string& body, bool replace, bool is_form_submission,
                                const NavigationReferrer* referrer) {
  HttpRequest request;
  request.url = url;
  request.owner_cancelled = cancelled_;
  request.method = method;
  request.body = body;
  // Document navigation negotiates HTML; curl's wildcard default can select
  // reduced representations that omit navigation and client-side page data.
  request.headers["Accept"] = "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8";
  request.allow_retries = !is_form_submission;
  request.is_form_submission = is_form_submission;
  if (method == "POST") request.headers["Content-Type"] = "application/x-www-form-urlencoded";
  const auto previous = current_;
  if (referrer) {
    request.referrer_source = referrer->source;
    request.referrer_policy = referrer->policy;
  } else if (previous) {
    previous->js->sync(previous->page);
    request.referrer_source = previous->page.url;
    request.referrer_policy = previous->page.referrer_policy;
  }
  if (previous) previous->js->suspend();
  std::shared_ptr<Document> document;
  try {
    auto response = perform_http(request);
    const bool error_document = response.status >= 400 && response.status < 600 &&
                                !response.body.empty() &&
                                is_html_content_type(response.content_type.c_str());
    if ((response.status < 200 || response.status >= 300) && !error_document)
      throw CliError(
          "HTTP_STATUS",
          "server returned HTTP " + std::to_string(response.status) + ": " + response.url,
          response.status >= 500 || response.status == 429);
    if (!is_html_content_type(response.content_type.c_str()) &&
        !is_plain_text_content_type(response.content_type))
      throw CliError("UNSUPPORTED_CONTENT_TYPE",
                     "expected HTML or plain text, got " + response.content_type);
    const auto offset = history_.empty() ? 0 : replace ? position_ : position_ + 1;
    const auto length = replace ? std::max<std::size_t>(1, history_.size()) : offset + 1;
    document = std::make_shared<Document>(
        std::move(response), storage_,
        HistoryContext{history_archive_, std::min<std::size_t>(100, length),
                       std::min<std::size_t>(99, offset), 0},
        ++next_history_group_, cancelled_);
  } catch (...) {
    if (previous) previous->js->resume();
    throw;
  }
  if (previous) previous->js->script_context().frame_tree->cancel_workers();
  current_ = std::move(document);
  Entry entry{current_, current_->page.document_url, nullptr, current_->history_group,
              current_->navigation_referrer};
  if (replace && !history_.empty())
    history_[position_] = entry;
  else {
    if (!history_.empty())
      history_.erase(history_.begin() + static_cast<std::ptrdiff_t>(position_ + 1), history_.end());
    history_.push_back(std::move(entry));
    position_ = history_.size() - 1;
  }
  trim_history();
}
void BrowsingInstance::trim_history() {
  constexpr std::size_t max_entries = 100, max_documents = 8;
  if (history_.size() > max_entries) {
    const auto removed = std::min(history_.size() - max_entries, position_);
    history_.erase(history_.begin(), history_.begin() + static_cast<std::ptrdiff_t>(removed));
    position_ -= removed;
    if (history_.size() > max_entries) history_.resize(max_entries);
  }
  // Include the active document in the budget, even during backward traversal.
  // Older entries retain URLs and can be refetched without keeping a JS heap.
  std::array<Document*, max_documents> retained{};
  retained[0] = current_.get();
  std::size_t count = current_ ? 1 : 0;
  for (auto it = history_.rbegin(); it != history_.rend(); ++it)
    if (it->document) {
      auto* ptr = it->document.get();
      const auto end = retained.begin() + static_cast<std::ptrdiff_t>(count);
      if (std::find(retained.begin(), end, ptr) != end) continue;
      if (count < max_documents)
        retained[count++] = ptr;
      else
        it->document.reset();
    }
  for (std::size_t i = 0; i < count; ++i) {
    retained[i]->js->retain_history(history_.size(), position_);
  }
}
void BrowsingInstance::snapshot() {
  if (!current_) return;
  current_->js->sync(current_->page);
  const auto revision = current_->js->observation_revision() ^
                        current_->js->script_context().frame_tree->observation_revision();
  if (revision != current_->observed_revision || current_->page.url != current_->observed_url) {
    current_->extractor.extract(current_->dom.get(), current_->page, *current_->js);
    current_->observed_revision = revision;
    current_->observed_url = current_->page.url;
  }
  append_script_error_log(current_->page, current_->logged_errors);
  current_->logged_errors = current_->page.script_errors.size();
}
void BrowsingInstance::move_history(int delta) {
  const auto target = static_cast<long long>(position_) + delta;
  if (target < 0 || target >= static_cast<long long>(history_.size())) return;
  auto& entry = history_[static_cast<std::size_t>(target)];
  const bool refetched = !entry.document;
  if (!entry.document) {
    const auto previous = current_;
    if (previous) previous->js->suspend();
    try {
      HttpRequest request;
      request.url = entry.url;
      request.owner_cancelled = cancelled_;
      if (entry.referrer) {
        request.referrer_source = entry.referrer->source;
        request.referrer_policy = entry.referrer->policy;
      }
      request.headers["Accept"] = "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8";
      auto response = perform_http(request);
      const bool error_document = response.status >= 400 && response.status < 600 &&
                                  !response.body.empty() &&
                                  is_html_content_type(response.content_type.c_str());
      if (((response.status < 200 || response.status >= 300) && !error_document) ||
          (!is_html_content_type(response.content_type.c_str()) &&
           !is_plain_text_content_type(response.content_type)))
        throw CliError("NAVIGATION_FAILED", "cannot restore history document");
      if (!same_origin(response.url, entry.url))
        throw CliError("HISTORY_ORIGIN_CHANGED",
                       "refetch changed origin; saved state was not exposed");
      auto restored = std::make_shared<Document>(
          std::move(response), storage_,
          HistoryContext{history_archive_, history_.size(), static_cast<std::size_t>(target),
                         entry.state ? entry.state->id : 0},
          entry.group, cancelled_);
      // All entries from this document share its refetched runtime. Traversing
      // between its virtual routes must not create another document or replay JS.
      for (auto& related : history_)
        if (related.group == entry.group) {
          related.document = restored;
          related.referrer = restored->navigation_referrer;
        }
    } catch (...) {
      if (previous) previous->js->resume();
      throw;
    }
  }
  if (current_ && current_ != entry.document) {
    current_->js->suspend();
    current_->js->script_context().frame_tree->cancel_workers();
  }
  current_ = entry.document;
  current_->js->resume();
  position_ = static_cast<std::size_t>(target);
  if (refetched) {
    current_->js->retain_history(history_.size(), position_);
    current_->js->notify_history_traversal();
  } else
    current_->js->restore_history(entry.url, entry.state ? entry.state->id : 0, history_.size(),
                                  position_);
  snapshot();
}
void BrowsingInstance::submit_form(int form_id, int button_id) {
  auto submission = prepare_form_navigation(*current_->js, form_id, button_id, current_->page.url);
  navigate(submission.url, submission.method, submission.body, false, true);
}
void BrowsingInstance::transitions(unsigned depth) {
  if (!current_) return;
  if (depth >= 8) throw CliError("NAVIGATION_LIMIT", "page exceeded eight scripted transitions");
  const auto source = current_;
  const auto history_actions = source->js->take_history_actions();
  auto navigation = current_->js->take_navigation();
  auto submission = current_->js->take_submission();
  current_->js->sync(current_->page);
  for (const auto& action : history_actions) {
    if (action.kind == HistoryAction::Kind::Traverse) {
      if (action.delta)
        move_history(action.delta);
      else {
        navigate(current_->page.url, "GET", "", true, false, current_->navigation_referrer.get());
        transitions(depth + 1);
        return;
      }
    } else {
      if (current_ != source) continue;
      Entry entry{source, action.url, action.state, source->history_group,
                  source->navigation_referrer};
      if (action.kind == HistoryAction::Kind::Replace && !history_.empty())
        history_[position_] = std::move(entry);
      else {
        if (!history_.empty())
          history_.erase(history_.begin() + static_cast<std::ptrdiff_t>(position_ + 1),
                         history_.end());
        history_.push_back(std::move(entry));
        position_ = history_.size() - 1;
      }
    }
  }
  if (!history_actions.empty()) trim_history();
  if (current_ != source) return;
  if (navigation) {
    navigate(navigation->url, "GET", "", navigation->replace, false,
             navigation->reload ? current_->navigation_referrer.get() : &navigation->referrer);
    transitions(depth + 1);
    return;
  }
  if (submission.first >= 0) {
    submit_form(submission.first, submission.second);
    transitions(depth + 1);
    return;
  }
}
void BrowsingInstance::tick() {
  if (cancelled_ && cancelled_->load())
    throw CliError("REQUEST_CANCELLED", "browsing instance closing");
  if (current_) {
    current_->js->pump();
    transitions();
  }
}
void BrowsingInstance::tick_background() {
  try {
    tick();
  } catch (const std::exception& error) {
    if (!current_) throw;
    auto& page = current_->page;
    if (page.script_errors.size() < kMaxScriptErrors)
      page.script_errors.push_back(std::string("background page task: ") + error.what());
  }
  // Log new errors even if the agent never asks for another observation.
  if (current_) {
    append_script_error_log(current_->page, current_->logged_errors);
    current_->logged_errors = current_->page.script_errors.size();
  }
}
int BrowsingInstance::find_node(const std::string& reference) const {
  const auto ref = normalized_ref(reference);
  for (const auto& link : current_->page.links)
    if (link.ref == ref) return link.node_id;
  for (const auto& button : current_->page.buttons)
    if (button.ref == ref) return button.node_id;
  for (const auto& field : current_->page.fields)
    if (field.ref == ref) return field.node_id;
  throw CliError("STALE_REF", "reference is absent from the current document: " + reference +
                                  "; run observe for current references");
}
int BrowsingInstance::find_frame(const std::string& reference) const {
  const auto ref = normalized_ref(reference);
  for (const auto& link : current_->page.links)
    if (link.ref == ref) return link.frame_id;
  for (const auto& button : current_->page.buttons)
    if (button.ref == ref) return button.frame_id;
  for (const auto& field : current_->page.fields)
    if (field.ref == ref) return field.frame_id;
  throw CliError("STALE_REF", "reference is absent from the current document");
}
std::unique_ptr<WaitOperation> BrowsingInstance::begin_wait(const EngineRequest& request) {
  validate_engine_request(request);
  if (request.action != BrowserAction::Wait)
    throw CliError("INVALID_ARGUMENT", "begin_wait requires a wait action");
  if (!current_) throw CliError("NO_PAGE", "no page loaded; use open <url>");
  if (!request.document.empty() && request.document != current_->page.document_id)
    throw CliError("STALE_DOCUMENT", "document changed; observe before acting");
  return std::make_unique<WaitOperation>(request);
}
const Page* BrowsingInstance::advance_wait(WaitOperation& operation) {
  tick();
  snapshot();
  bool ready = false;
  if (operation.mode == WaitCondition::Idle) {
    if (current_->js->network_pending()) operation.idle_since = Clock::now();
    ready = Clock::now() - operation.idle_since >= std::chrono::milliseconds(100);
  } else if (operation.mode == WaitCondition::Text)
    ready = current_->page.text.find(operation.value) != std::string::npos;
  else if (operation.mode == WaitCondition::Gone)
    ready = current_->page.text.find(operation.value) == std::string::npos;
  else if (operation.mode == WaitCondition::Selector)
    ready = current_->js->matches(operation.value) ||
            current_->js->script_context().frame_tree->matches(operation.value);
  else {
    try {
      find_node(operation.value);
      ready = true;
    } catch (const CliError& error) {
      if (error.code != "STALE_REF") throw;
    }
  }
  if (ready) return &current_->page;
  if (Clock::now() >= operation.deadline)
    throw CliError("WAIT_TIMEOUT", "condition did not match before timeout", true);
  return nullptr;
}
const Page& BrowsingInstance::execute(const EngineRequest& request) {
  validate_engine_request(request);
  if (request.action == BrowserAction::Open) {
    const NavigationReferrer direct;
    navigate(normalize_url(request.target), "GET", "", false, false, &direct);
    transitions();
    tick();
    snapshot();
    return current_->page;
  }
  if (!current_) throw CliError("NO_PAGE", "no page loaded; use open <url>");
  tick();
  snapshot();
  if (!request.document.empty() && request.document != current_->page.document_id)
    throw CliError("STALE_DOCUMENT", "document changed; observe before acting");
  const auto command = request.action;
  if (command == BrowserAction::Observe) return current_->page;
  if (command == BrowserAction::Wait) {
    auto operation = begin_wait(request);
    while (true) {
      if (const auto* page = advance_wait(*operation)) return *page;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }
  const auto id = find_node(request.target);
  const auto frame_id = find_frame(request.target);
  auto* runtime =
      frame_id ? current_->js->script_context().frame_tree->runtime(frame_id) : current_->js.get();
  if (!runtime) throw CliError("STALE_REF", "frame was removed; run observe");
  std::string expression = "const n=__wrap(" + std::to_string(id) + ");";
  if (command == BrowserAction::Click)
    expression += "__inputClick(n);";
  else if (command == BrowserAction::Keyboard) {
    const auto& key = request.value;
    auto* target = runtime->node(id);
    const bool disabled = is_disabled_control(target);
    if (disabled) throw CliError("FIELD_DISABLED", "control is disabled");
    expression += "__keyboard(n," + json_escape(key) + ");";
  } else if (command == BrowserAction::Fill || command == BrowserAction::Select) {
    auto field =
        std::find_if(current_->page.fields.begin(), current_->page.fields.end(),
                     [&](const Field& f) { return f.node_id == id && f.frame_id == frame_id; });
    if (field == current_->page.fields.end())
      throw CliError("INVALID_TARGET", "fill/select requires an input or select ref");
    if (field->disabled) throw CliError("FIELD_DISABLED", "field is disabled");
    if (command == BrowserAction::Fill && field->readonly)
      throw CliError("FIELD_READONLY", "field is readonly");
    if (command == BrowserAction::Fill && field->type == "select")
      throw CliError("INVALID_TARGET", "use select with an S ref");
    if (command == BrowserAction::Select && field->type != "select")
      throw CliError("INVALID_TARGET", "select requires an S ref");
    if (command == BrowserAction::Select && has_attr(runtime->node(id), "multiple"))
      throw CliError("UNSUPPORTED_MULTISELECT", "multiple selects are unsupported");
    if (field->type == "image")
      throw CliError("UNSUPPORTED_IMAGE_SUBMIT", "image submit controls are unsupported");
    if (field->type == "file")
      throw CliError("UNSUPPORTED_FILE_UPLOAD", "file uploads are unsupported");
    const auto& value = request.value;
    if (value.size() > 65536) throw CliError("VALUE_TOO_LARGE", "field value exceeds 64 KiB");
    if (field->type == "select" &&
        std::find(field->options.begin(), field->options.end(), value) == field->options.end())
      throw CliError("INVALID_OPTION", "select value must match an observed option");
    // Focus before editing, so page editors can establish their active control
    // and synchronize their model through focus and subsequent input events.
    expression += "n.focus();";
    if (field->type == "contenteditable") {
      expression += "n.textContent=" + json_escape(value) + ";";
    } else if (field->type == "checkbox" || field->type == "radio") {
      if (value != "true" && value != "false")
        throw CliError("INVALID_ARGUMENT", "checkbox/radio expects true or false");
      expression += "__setControlProperty(n,'checked'," + value + ");";
    } else {
      if (command == BrowserAction::Select)
        expression += "const option=n.options.find(o=>o.value===" + json_escape(value) +
                      ");if(option.disabled || option.closest('optgroup[disabled]'))throw new "
                      "Error('option is disabled');";
      expression += "__setControlProperty(n,'value'," + json_escape(value) + ");";
    }
    expression +=
        "__dispatchInputEvent(n,new Event('input',{bubbles:true,composed:true}));"
        "__dispatchInputEvent(n,new Event('change',{bubbles:true}));";
  } else
    throw CliError("UNKNOWN_COMMAND", "unsupported browsing command");
  const auto errors = current_->page.script_errors.size();
  if (!runtime->evaluate("(function(){" + expression + "})()",
                         std::string("agent ") + action_name(command))) {
    auto message = current_->page.script_errors.size() > errors
                       ? current_->page.script_errors.back()
                       : "page interaction failed";
    snapshot();
    current_->page.script_errors.resize(errors);
    current_->logged_errors = errors;
    throw CliError("ACTION_FAILED", message);
  }
  transitions();
  tick();
  snapshot();
  return current_->page;
}
}  // namespace webcli
