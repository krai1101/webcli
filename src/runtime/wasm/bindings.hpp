#pragma once
#include <quickjs.h>
namespace webcli {
struct ScriptContext;
void install_wasm_bindings(JSContext*, JSValueConst);
void shutdown_wasm(ScriptContext&);
}  // namespace webcli
