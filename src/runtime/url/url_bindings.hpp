#pragma once
#include <quickjs.h>
namespace webcli {
JSValue js_url_parse(JSContext*, JSValueConst, int, JSValueConst*);
}
