#pragma once
#include <quickjs.h>
namespace webcli {
JSValue js_blob_url_register(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_blob_url_revoke(JSContext*, JSValueConst, int, JSValueConst*);
}  // namespace webcli
