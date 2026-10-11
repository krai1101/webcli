#pragma once
#include <quickjs.h>
namespace webcli {
struct ScriptContext;
JSValue js_performance_now(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_native_queue_microtask(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_native_set_timeout(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_native_clear_timeout(JSContext*, JSValueConst, int, JSValueConst*);
}  // namespace webcli
