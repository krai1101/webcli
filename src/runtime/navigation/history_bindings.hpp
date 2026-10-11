#pragma once

#include <quickjs.h>

namespace webcli {
JSValue js_history_commit(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_history_traverse(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_history_read(JSContext*, JSValueConst, int, JSValueConst*);
}  // namespace webcli
