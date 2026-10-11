#include "runtime/navigation/history_clone_bindings.hpp"

namespace webcli {
JSValue js_is_proxy(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  return JS_NewBool(context, argc > 0 && JS_IsProxy(argv[0]));
}
}  // namespace webcli
