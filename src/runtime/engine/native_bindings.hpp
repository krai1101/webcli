#pragma once
#include <quickjs.h>

namespace webcli {
// Install guarded host callbacks before evaluating any page JavaScript.
void install_native_bindings(JSContext* context, JSValueConst global);
}  // namespace webcli
