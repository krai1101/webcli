#pragma once
#include <quickjs.h>

namespace webcli {
JSValue js_crypto_digest(JSContext*, JSValueConst, int, JSValueConst*);
}  // namespace webcli
