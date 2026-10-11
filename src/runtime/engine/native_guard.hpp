#pragma once
#include <quickjs.h>

#include <exception>

#include "runtime/engine/script_context.hpp"

namespace webcli {
// QuickJS invokes C functions: C++ exceptions must terminate at this boundary.
template <JSCFunction* Function>
JSValue guarded_binding(JSContext* context, JSValueConst self, int argc,
                        JSValueConst* argv) noexcept {
  try {
    auto* state = static_cast<ScriptContext*>(JS_GetContextOpaque(context));
    if (state && state->owner_thread != std::this_thread::get_id())
      return JS_ThrowInternalError(context, "browser binding called outside its owner thread");
    if (state && state->frame_id && !state->active)
      return JS_ThrowTypeError(context, "frame document is no longer active");
    return Function(context, self, argc, argv);
  } catch (const std::exception& error) {
    return JS_ThrowInternalError(context, "%s", error.what());
  } catch (...) {
    return JS_ThrowInternalError(context, "unexpected native exception");
  }
}
}  // namespace webcli
