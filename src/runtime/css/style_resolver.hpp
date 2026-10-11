#pragma once
#include <quickjs.h>

#include "runtime/css/visibility_rules.hpp"

namespace webcli {
JSValue js_popover_state(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_dom_revision(JSContext*, JSValueConst, int, JSValueConst*);
struct ScriptContext;
const VisibilityValues& resolve_visibility(ScriptContext&);
JSValue js_computed_visibility(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_style_invalidate(JSContext*, JSValueConst, int, JSValueConst*);
JSValue js_adopted_stylesheet(JSContext*, JSValueConst, int, JSValueConst*);
}  // namespace webcli
