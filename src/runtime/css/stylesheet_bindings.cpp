#include "runtime/css/stylesheet_bindings.hpp"

#include "runtime/dom/dom_bindings.hpp"
#include "runtime/dom/dom_helpers.hpp"
#include "runtime/engine/js_handles.hpp"
#include "runtime/engine/script_context.hpp"

namespace webcli {
JSValue js_inline_stylesheet(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  auto* node = argc ? dom_node_arg(context, argv[0]) : nullptr;
  if (!node || (tag_name(node) != "style" && tag_name(node) != "link")) return JS_NULL;
  auto& state = *static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  const bool external = tag_name(node) == "link";
  const auto loaded = state.css.stylesheets.find(node);
  if (external && (loaded == state.css.stylesheets.end() || !loaded->second.source ||
                   loaded->second.href != attr(node, "href")))
    return JS_NULL;
  const auto original = external ? attr(node, "href") : node_text(node);
  auto found = state.css.inline_stylesheets.find(node);
  if (found != state.css.inline_stylesheets.end() && found->second.original != original) {
    state.css.inline_stylesheet_bytes -=
        found->second.original.size() + found->second.source.size();
    state.css.inline_stylesheets.erase(found);
    found = state.css.inline_stylesheets.end();
  }
  if (argc > 1) {
    auto source = js_string(context, argv[1]);
    const auto previous = found == state.css.inline_stylesheets.end()
                              ? 0
                              : found->second.source.size() + found->second.original.size();
    const auto bytes = source.size() + original.size();
    if (bytes > 2 * 1024 * 1024 - (state.css.inline_stylesheet_bytes - previous))
      return JS_ThrowInternalError(context, "inline stylesheet storage exceeds 2 MiB");
    state.css.inline_stylesheets.insert_or_assign(node,
                                                  InlineStylesheet{original, std::move(source)});
    state.css.inline_stylesheet_bytes = state.css.inline_stylesheet_bytes - previous + bytes;
    ++state.css.style_generation;
    return JS_UNDEFINED;
  }
  const auto& source = found != state.css.inline_stylesheets.end() ? found->second.source
                       : external                                  ? *loaded->second.source
                                                                   : original;
  return JS_NewStringLen(context, source.data(), source.size());
}

JSValue js_stylesheet_disabled(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  auto* node = argc ? dom_node_arg(context, argv[0]) : nullptr;
  if (!node || (tag_name(node) != "style" && tag_name(node) != "link"))
    return JS_ThrowTypeError(context, "stylesheet owner required");
  auto& state = *static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  if (argc > 1) {
    if (JS_ToBool(context, argv[1]))
      state.css.disabled_stylesheets.insert(node);
    else
      state.css.disabled_stylesheets.erase(node);
    ++state.css.style_generation;
  }
  return JS_NewBool(context, state.css.disabled_stylesheets.count(node) != 0);
}
}  // namespace webcli
