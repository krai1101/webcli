#include "runtime/scripts/parser_coordinator.hpp"

#include <lexbor/html/parser.h>
#include <lexbor/html/tokenizer.h>
#include <lexbor/html/tree.h>
#include <lexbor/html/tree/open_elements.h>

#include <algorithm>
#include <exception>
#include <memory>
#include <unordered_set>
#include <vector>

#include "runtime/dom/dom_bindings.hpp"
#include "runtime/dom/dom_helpers.hpp"
#include "runtime/dom/tree_usage.hpp"
#include "runtime/engine/js_handles.hpp"
#include "runtime/engine/js_runtime.hpp"
#include "runtime/engine/script_context.hpp"
#include "runtime/network/referrer_policy.hpp"
#include "runtime/scripts/document_executor.hpp"
#include "runtime/scripts/document_scripts.hpp"

namespace webcli {
namespace {
class ParserCoordinator {
 public:
  ParserCoordinator(JsRuntime& runtime, ScriptContext& state, Page& page)
      : runtime_(runtime), state_(state), page_(page) {
    if (lxb_html_document_parse_chunk_begin(state.dom.document) != LXB_STATUS_OK)
      throw CliError("HTML_PARSE_FAILED", "cannot begin live HTML parsing");
    auto* parser = static_cast<lxb_html_parser_t*>(state.dom.document->dom_document.parser);
    tokenizer_ = lxb_html_parser_tokenizer(parser);
    original_ = tokenizer_->callback_token_done;
    original_context_ = tokenizer_->callback_token_ctx;
    tokenizer_->callback_token_done = token;
    tokenizer_->callback_token_ctx = this;
    state_.parser_write = [this](std::string source) {
      if (source.size() > kDomFragmentBytes || source.size() > kDomAllocationBytes - written_bytes_)
        throw CliError("DOM_QUOTA", "document.write input exceeds parser allocation limits");
      written_bytes_ += source.size();
      if (++write_depth_ > 32) {
        --write_depth_;
        throw CliError("PARSER_RECURSION", "nested document.write exceeds 32 levels");
      }
      try {
        // Tokenizer spans may retain pointers until a following token arrives.
        // Written input therefore lives for the entire parser lifetime.
        written_.push_back(std::make_unique<std::string>(std::move(source)));
        feed(*written_.back());
      } catch (...) {
        --write_depth_;
        throw;
      }
      --write_depth_;
    };
  }
  ~ParserCoordinator() {
    state_.parser_write = {};
    tokenizer_->callback_token_done = original_;
    tokenizer_->callback_token_ctx = original_context_;
  }
  void feed(std::string_view source) {
    for (std::size_t offset = 0; offset < source.size();) {
      if (state_.execution.cancelled())
        throw CliError("REQUEST_CANCELLED", "document loading cancelled");
      // A chunk ends at a token delimiter. Once a closing script token is
      // delivered, no subsequent markup is fed before its script executes.
      const auto delimiter = source.find('>', offset);
      const auto end = delimiter == std::string_view::npos ? source.size() : delimiter + 1;
      if (lxb_html_document_parse_chunk(state_.dom.document,
                                        reinterpret_cast<const lxb_char_t*>(source.data() + offset),
                                        end - offset) != LXB_STATUS_OK)
        throw CliError("HTML_PARSE_FAILED", "live HTML parser failed");
      if (failure_) std::rethrow_exception(failure_);
      const bool script_changed_styles = styles_revision_ != state_.css.style_generation;
      ++state_.css.style_generation;
      if (!script_changed_styles) styles_revision_ = state_.css.style_generation;
      const auto chunk_length = end - offset;
      offset = end;
      if (pending_template_) {
        account_tree();
        auto* completed_template = pending_template_;
        auto* scope = completed_template->parent;
        pending_template_ = nullptr;
        if (scope) {
          runtime_.evaluate("__adoptDeclarativeTemplates(__wrap(" +
                                std::to_string(dom_node_id(&state_, completed_template)) + "));",
                            "parser declarative roots");
          // Only scripts moved into a valid declarative root are executable.
          // Ordinary template contents remain inert.
          const auto root = state_.dom.shadow_roots.find(scope);
          if (root != state_.dom.shadow_roots.end()) {
            std::vector<DocumentScript> scripts;
            collect_document_scripts(state_, root->second, page_.url, scripts);
            for (const auto& script : scripts) handle_script(script.node);
          }
        }
      }
      if (pending_component_) {
        account_tree();
        auto* component = pending_component_;
        pending_component_ = nullptr;
        runtime_.evaluate(
            "__connectComponents(__wrap(" + std::to_string(dom_node_id(&state_, component)) + "));",
            "parser component reactions");
        runtime_.checkpoint();
      }
      if (pending_script_) {
        account_tree();
        auto* script = pending_script_;
        pending_script_ = nullptr;
        handle_script(script);
      }
      processed_ += chunk_length;
      total_processed_ += chunk_length;
      if (processed_ >= std::max<std::size_t>(16384, total_processed_ / 8)) {
        account_tree();
        processed_ = 0;
      }
    }
  }
  void finish() {
    if (lxb_html_document_parse_chunk_end(state_.dom.document) != LXB_STATUS_OK)
      throw CliError("HTML_PARSE_FAILED", "cannot finish live HTML parser");
    if (failure_) std::rethrow_exception(failure_);
    // EOF may flush buffered text or create implied nodes after the last chunk.
    ++state_.css.style_generation;
    account_tree();
    load_styles();
    execute_document_scripts(runtime_, state_, page_, deferred_, true);
  }

 private:
  void account_tree() {
    if (accounted_revision_ == state_.css.style_generation) return;
    const auto usage = measure_tree(lxb_dom_interface_node(&state_.dom.document->dom_document));
    state_.dom.allocations.observe_live_usage(usage.bytes, usage.nodes);
    accounted_revision_ = state_.css.style_generation;
  }
  void load_styles() {
    if (!styles_dirty_ && styles_revision_ == state_.css.style_generation) return;
    state_.css.stylesheets = load_visibility_stylesheets(
        lxb_dom_interface_node(&state_.dom.document->dom_document), page_.url, page_,
        &state_.css.stylesheets, state_.referrer_policy, state_.cancelled);
    ++state_.css.style_generation;
    styles_revision_ = state_.css.style_generation;
    styles_dirty_ = false;
  }
  static lxb_html_token_t* token(lxb_html_tokenizer_t* tokenizer, lxb_html_token_t* value,
                                 void* opaque) noexcept {
    auto& self = *static_cast<ParserCoordinator*>(opaque);
    auto* current = lxb_html_tree_current_node(tokenizer->tree);
    const bool script_end = value->tag_id == LXB_TAG_SCRIPT &&
                            (value->type & LXB_HTML_TOKEN_TYPE_CLOSE) && current &&
                            current->ns == LXB_NS_HTML && current->local_name == LXB_TAG_SCRIPT;
    auto* template_node = lxb_html_tree_open_elements_find_reverse(
        tokenizer->tree, LXB_TAG_TEMPLATE, LXB_NS_HTML, nullptr);
    const bool template_end =
        value->tag_id == LXB_TAG_TEMPLATE && (value->type & LXB_HTML_TOKEN_TYPE_CLOSE);
    if (value->tag_id == LXB_TAG_LINK || value->tag_id == LXB_TAG_BASE) self.styles_dirty_ = true;
    auto* result = self.original_(tokenizer, value, self.original_context_);
    if (script_end && !template_node) self.pending_script_ = current;
    if (template_end && template_node &&
        !lxb_html_tree_open_elements_find_reverse(tokenizer->tree, LXB_TAG_TEMPLATE, LXB_NS_HTML,
                                                  nullptr))
      self.pending_template_ = template_node;
    if (!(value->type & LXB_HTML_TOKEN_TYPE_CLOSE)) {
      try {
        if (value->tag_id == LXB_TAG_META && !template_node) {
          auto* parent = lxb_html_tree_current_node(tokenizer->tree);
          if (parent) process_referrer_meta(self.state_, parent->last_child);
        }
        auto* inserted = lxb_html_tree_current_node(tokenizer->tree);
        if (inserted && inserted != current && inserted->ns == LXB_NS_HTML &&
            tag_name(inserted).find('-') != std::string::npos)
          self.pending_component_ = inserted;
      } catch (...) {
        self.failure_ = std::current_exception();
      }
    }
    return result;
  }
  void handle_script(lxb_dom_node_t* node) {
    if (!state_.scripting_allowed) return;
    if (!processed_scripts_.insert(node).second) return;
    if (++script_count_ > kMaxDocumentScripts)
      throw CliError("SCRIPT_QUOTA", "parser script count exceeds document limit");
    std::vector<DocumentScript> scripts;
    const JsValueOwner global(state_.context, JS_GetGlobalObject(state_.context));
    const JsValueOwner document(state_.context,
                                JS_GetPropertyStr(state_.context, global.get(), "document"));
    const auto base = property_string(state_.context, document.get(), "baseURI", page_.url);
    collect_document_scripts(state_, node, base, scripts);
    if (scripts.empty()) return;
    load_styles();
    auto script = std::move(scripts.front());
    script.base_url = base;
    script.referrer_policy = element_referrer_policy(state_.referrer_policy, script.node);
    const bool defer = !script.import_map && !is_async_document_script(script) &&
                       (script.module || (!script.url.empty() && has_attr(script.node, "defer")));
    if (defer) {
      if (script.module && !script.url.empty()) {
        try {
          state_.modules->remember_script(script.url, script.url,
                                          {page_.url, script.referrer_policy});
          state_.modules->preload(script.url);
        } catch (const std::exception& error) {
          script.failure = error.what();
        }
      }
      deferred_.push_back(std::move(script));
    } else {
      std::vector<DocumentScript> immediate;
      immediate.push_back(std::move(script));
      execute_document_scripts(runtime_, state_, page_, immediate, false);
    }
  }
  JsRuntime& runtime_;
  ScriptContext& state_;
  Page& page_;
  lxb_html_tokenizer_t* tokenizer_ = nullptr;
  lxb_html_tokenizer_token_f original_ = nullptr;
  void* original_context_ = nullptr;
  lxb_dom_node_t* pending_script_ = nullptr;
  lxb_dom_node_t* pending_component_ = nullptr;
  lxb_dom_node_t* pending_template_ = nullptr;
  std::exception_ptr failure_;
  std::vector<DocumentScript> deferred_;
  std::unordered_set<lxb_dom_node_t*> processed_scripts_;
  std::vector<std::unique_ptr<std::string>> written_;
  std::size_t processed_ = 0, script_count_ = 0, written_bytes_ = 0;
  std::uint64_t accounted_revision_ = 0, styles_revision_ = 0;
  bool styles_dirty_ = true;
  std::size_t total_processed_ = 0;
  unsigned write_depth_ = 0;
};
}  // namespace
void parse_live_document(JsRuntime& runtime, ScriptContext& state, Page& page,
                         std::string_view source) {
  if (source.size() >= 3 && source.substr(0, 3) == "\xEF\xBB\xBF") source.remove_prefix(3);
  state.script_loading.begin();
  ParserCoordinator parser(runtime, state, page);
  parser.feed(source);
  parser.finish();
}
JSValue js_document_write(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  auto& state = *static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  if (!state.parser_write)
    return JS_ThrowTypeError(context, "document.write requires an active parser");
  if (argc) state.parser_write(js_string(context, argv[0]));
  return JS_UNDEFINED;
}
}  // namespace webcli
