#pragma once
#include <quickjs.h>
namespace webcli {
struct ScriptContext;
struct HttpRequest;
struct HttpResponse;
HttpResponse load_script_resource(const HttpRequest&);
JSValue enqueue_http(ScriptContext&, HttpRequest, bool script = false, bool opaque = false);
void start_queued_http(ScriptContext&);
JSValue js_native_script_kind(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_native_image_load(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_native_script_load(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_native_script_execute(JSContext*, JSValueConst, int, JSValueConst*);
}  // namespace webcli
