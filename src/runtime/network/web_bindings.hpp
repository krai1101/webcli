#pragma once
#include <quickjs.h>
namespace webcli {
JSValue js_report_error(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_native_resolve(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_native_navigate(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_native_fetch(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_native_cancel_fetch(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_native_random_bytes(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_native_cookie(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_native_storage(JSContext*, JSValueConst, int, JSValueConst*);
}  // namespace webcli
