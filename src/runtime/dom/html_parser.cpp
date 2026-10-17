#include "runtime/dom/html_parser.hpp"

#include "runtime/dom/document_loader.hpp"
#include "runtime/dom/dom_bindings.hpp"
#include "runtime/dom/tree_usage.hpp"
#include "runtime/engine/js_handles.hpp"
#include "runtime/engine/script_context.hpp"

namespace webcli {
struct ParsedDocumentOwner {
  // QuickJS may finalize a detached document after its frame's realm lease.
  // Keep registry/CSS state and the frame's native document alive until this
  // finalizer has removed its own native pointers from those registries.
  std::shared_ptr<ScriptContext> state_owner;
  ScriptContext& state;
  DomState::ParsedDocument document{nullptr, lxb_html_document_destroy};
  TreeUsage usage;
  bool charged = false;
  explicit ParsedDocumentOwner(ScriptContext& context)
      : state_owner(context.shared_from_this()), state(context) {}
  ~ParsedDocumentOwner() {
    if (!document) return;
    auto* owner = &document->dom_document;
    const auto belongs = [owner](lxb_dom_node_t* node) {
      return node && node->owner_document == owner;
    };
    auto& dom = state.dom;
    for (std::size_t id = 0; id < dom.nodes.size(); ++id) {
      auto* node = dom.nodes[id];
      if (!belongs(node)) continue;
      dom.node_ids.erase(node);
      dom.nodes[id] = nullptr;
      // A finalizer must not throw if reuse bookkeeping cannot grow.
      try {
        dom.free_node_ids.push_back(static_cast<int>(id));
      } catch (...) {
      }
    }
    const auto erase_owned = [&](auto& values) {
      for (auto it = values.begin(); it != values.end();)
        if (belongs(it->first))
          it = values.erase(it);
        else
          ++it;
    };
    erase_owned(dom.selected_options);
    erase_owned(dom.textarea_values);
    erase_owned(dom.modal_dialogs);
    erase_owned(dom.parser_form_owners);
    erase_owned(dom.shadow_roots);
    erase_owned(dom.shadow_hosts);
    erase_owned(dom.assigned_slots);
    erase_owned(dom.slot_children);
    for (auto it = dom.open_popovers.begin(); it != dom.open_popovers.end();)
      if (belongs(*it))
        it = dom.open_popovers.erase(it);
      else
        ++it;
    erase_owned(state.css.visibility_values);
    erase_owned(state.css.stylesheets);
    for (auto it = state.css.inline_stylesheets.begin(); it != state.css.inline_stylesheets.end();)
      if (belongs(it->first)) {
        state.css.inline_stylesheet_bytes -= it->second.original.size() + it->second.source.size();
        it = state.css.inline_stylesheets.erase(it);
      } else
        ++it;
    for (auto it = state.css.adopted_stylesheets.begin();
         it != state.css.adopted_stylesheets.end();)
      if (belongs(it->first)) {
        state.css.adopted_stylesheet_bytes -= it->second.size();
        it = state.css.adopted_stylesheets.erase(it);
      } else
        ++it;
    for (auto it = state.css.disabled_stylesheets.begin();
         it != state.css.disabled_stylesheets.end();)
      if (belongs(*it))
        it = state.css.disabled_stylesheets.erase(it);
      else
        ++it;
    dom.parsed_documents.erase(owner);
    if (charged) dom.allocations.release_tree(usage.bytes, usage.nodes);
  }
};
void release_attribute_bytes(ScriptContext& state, lxb_dom_node_t* node, std::size_t bytes) {
  const auto found = state.dom.parsed_documents.find(node->owner_document);
  if (found != state.dom.parsed_documents.end()) {
    // Replaced buffers have already been released from global accounting.
    // Exclude them from the owner's eventual release as well. Later mutations
    // remain conservatively charged until page teardown.
    auto& initial_bytes = found->second->usage.bytes;
    initial_bytes -= bytes > initial_bytes ? initial_bytes : bytes;
  }
  state.dom.allocations.release_bytes(bytes);
}
namespace {
void finalize_parsed_document(JSRuntime*, JSValue value) {
  delete static_cast<ParsedDocumentOwner*>(JS_GetOpaque(value, JS_GetClassID(value)));
}
void install_parsed_document_class(ScriptContext& state) {
  if (state.parsed_document_class) return;
  JS_NewClassID(state.runtime, &state.parsed_document_class);
  JSClassDef definition{};
  definition.class_name = "ParsedDocumentOwner";
  definition.finalizer = finalize_parsed_document;
  if (JS_NewClass(state.runtime, state.parsed_document_class, &definition) < 0)
    throw CliError("RUNTIME_INIT", "cannot register parsed document ownership");
}
}  // namespace
JSValue js_parse_html_document(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  if (!argc) return JS_ThrowTypeError(context, "HTML source required");
  auto& state = *static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  const JsCString source(context, argv[0]);
  if (source.size() > kMaxBodyBytes)
    return JS_ThrowInternalError(context, "DOMParser source exceeds the response body limit");
  if (state.dom.parsed_documents.size() >= 32) JS_RunGC(state.runtime);
  if (state.dom.parsed_documents.size() >= 32)
    return JS_ThrowInternalError(context, "DOMParser document quota exceeded");
  auto owner = std::make_unique<ParsedDocumentOwner>(state);
  owner->document.reset(lxb_html_document_create());
  if (!owner->document) return JS_ThrowOutOfMemory(context);
  parse_bounded_document(owner->document.get(), {source.get(), source.size()});
  auto* root = lxb_dom_interface_node(&owner->document->dom_document);
  owner->usage = measure_tree(root);
  try {
    state.dom.allocations.reserve(owner->usage.bytes, owner->usage.nodes);
  } catch (const CliError&) {
    // Retry accounting only after collecting unreachable detached documents.
    // Retained wrappers keep their entire native owner alive.
    JS_RunGC(state.runtime);
    state.dom.allocations.reserve(owner->usage.bytes, owner->usage.nodes);
  }
  owner->charged = true;
  install_parsed_document_class(state);
  const JsValueOwner lease(context, JS_NewObjectClass(context, state.parsed_document_class));
  if (JS_IsException(lease.get())) return JS_EXCEPTION;
  const auto id = dom_node_id(&state, root);
  state.dom.parsed_documents.emplace(&owner->document->dom_document, owner.get());
  JS_SetOpaque(lease.get(), owner.release());
  if (JS_SetPropertyStr(context, lease.get(), "rootId", JS_NewInt32(context, id)) < 0)
    return JS_EXCEPTION;
  return JS_DupValue(context, lease.get());
}
JSValue js_node_owner_document(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  auto* node = argc ? dom_node_arg(context, argv[0]) : nullptr;
  if (!node || node->type == LXB_DOM_NODE_TYPE_DOCUMENT) return JS_NewInt32(context, -1);
  auto* state = static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  return JS_NewInt32(context, dom_node_id(state, lxb_dom_interface_node(node->owner_document)));
}
}  // namespace webcli
