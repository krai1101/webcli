#pragma once
#include <quickjs.h>
namespace webcli {
JSValue js_inline_stylesheet(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_stylesheet_disabled(JSContext*, JSValueConst, int, JSValueConst*);
}  // namespace webcli
