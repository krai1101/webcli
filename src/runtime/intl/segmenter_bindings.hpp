#pragma once
#include <quickjs.h>

namespace webcli {
void install_segmenter_class(JSContext*);
JSValue js_segmenter(JSContext*, JSValueConst, int, JSValueConst*);
}  // namespace webcli
