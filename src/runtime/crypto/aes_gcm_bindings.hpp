#pragma once
#include <quickjs.h>

namespace webcli {
JSValue js_crypto_aes_gcm(JSContext*, JSValueConst, int, JSValueConst*);
}  // namespace webcli
