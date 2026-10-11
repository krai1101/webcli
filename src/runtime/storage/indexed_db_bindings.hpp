#pragma once
#include <quickjs.h>

namespace webcli {
JSValue js_indexed_db(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_indexed_db_codec(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_task_generation(JSContext*, JSValueConst, int, JSValueConst*);
}  // namespace webcli
