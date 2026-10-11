#pragma once
#include <quickjs.h>

namespace webcli {
void install_number_format_class(JSContext*);
JSValue js_number_format(JSContext*, JSValueConst, int, JSValueConst*);
}  // namespace webcli
