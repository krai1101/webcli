#pragma once
#include <quickjs.h>

#include <string>
namespace webcli {
struct ScriptContext;
std::string js_error(JSContext*, JSValueConst);
bool begin_js_task(ScriptContext*) noexcept;
void record_async_js_error(ScriptContext*, JSContext*) noexcept;
void drain_timer_jobs(ScriptContext*);
void promise_rejection_tracker(JSContext*, JSValueConst, JSValueConst, bool, void*) noexcept;
void flush_promise_rejections(ScriptContext&) noexcept;
void clear_promise_rejections(ScriptContext&) noexcept;
}  // namespace webcli
