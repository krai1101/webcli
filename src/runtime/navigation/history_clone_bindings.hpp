#pragma once
#include <quickjs.h>
namespace webcli {
JSValue js_is_proxy(JSContext*, JSValueConst, int, JSValueConst*);
}  // namespace webcli
