#include "runtime/wasm/bindings.hpp"

#include "runtime/engine/js_handles.hpp"
#include "runtime/engine/script_context.hpp"
#include "runtime/wasm/objects.hpp"

namespace webcli {
namespace {
JSValue operate(JSContext* context, JSValueConst, int argc, JSValueConst* argv, int,
                JSValueConst* data) noexcept {
  try {
    const auto* lifetime = static_cast<WasmHandle*>(JS_GetOpaque(data[0], JS_GetClassID(data[0])));
    if (!lifetime || !lifetime->scope->active)
      return JS_ThrowTypeError(context, "WebAssembly realm is closed");
    lifetime->scope->state->require_owner();
    if (argc < 1) return JS_ThrowTypeError(context, "WebAssembly operation required");
    const auto action = js_string(context, argv[0]);
    wasm_handle_class(context);
    const auto scope = static_cast<ScriptContext*>(JS_GetContextOpaque(context))->wasm;
    WasmAllocation allocation(scope->account);
    if (action == "allocatedBytes")
      return JS_NewFloat64(context, double(scope->account->used.load()));
    const auto input = argc > 1 ? argv[1] : JS_UNDEFINED;
    const auto value = argc > 2 ? argv[2] : JS_UNDEFINED;
    if (action == "isFunction") {
      const auto* handle =
          static_cast<WasmHandle*>(JS_GetOpaque(input, wasm_handle_class(context)));
      return JS_NewBool(context, handle && handle->kind == WasmKind::Function);
    }
    if (action == "validate") {
      const auto compiled = wasm_compile(context, input);
      if (JS_IsException(compiled)) {
        const auto exception = JS_GetException(context);
        const auto name = property_string(context, exception, "name");
        if (name != "CompileError") return JS_Throw(context, exception);
        JS_FreeValue(context, exception);
        return JS_FALSE;
      }
      JS_FreeValue(context, compiled);
      return JS_TRUE;
    }
    if (action == "compile") return wasm_compile(context, input);
    if (action == "imports" || action == "exports")
      return wasm_describe(context, wasm_handle(context, input, WasmKind::Module),
                           action == "imports");
    if (action == "instantiate")
      return wasm_instantiate(context, wasm_handle(context, input, WasmKind::Module), value);
    if (action == "instanceExports")
      return wasm_exports(context, wasm_handle(context, input, WasmKind::Instance), input);
    if (action == "call")
      return wasm_call(context, wasm_handle(context, input, WasmKind::Function), value);
    if (action == "buffer")
      return wasm_memory_buffer(context, wasm_handle(context, input, WasmKind::Memory), input);
    if (action == "globalGet")
      return wasm_global_get(context, wasm_handle(context, input, WasmKind::Global));
    if (action == "globalSet")
      return wasm_global_set(context, wasm_handle(context, input, WasmKind::Global), value);
    uint32_t number = 0;
    if (action != "length" && JS_ToUint32(context, &number, value) < 0) return JS_EXCEPTION;
    if (action == "memoryGrow")
      return wasm_memory_grow(context, wasm_handle(context, input, WasmKind::Memory), number);
    if (action == "length" || action == "get" || action == "set" || action == "grow" ||
        action == "tableType")
      return wasm_table_access(context, wasm_handle(context, input, WasmKind::Table), action,
                               number, argc > 3 ? argv[3] : JS_NULL, input);
    return JS_ThrowTypeError(context, "Unknown WebAssembly operation");
  } catch (const std::invalid_argument& error) {
    if (JS_HasException(context)) return JS_EXCEPTION;
    return JS_ThrowTypeError(context, "%s", error.what());
  } catch (const std::bad_alloc&) {
    return JS_ThrowOutOfMemory(context);
  } catch (const std::exception& error) {
    return wasm_error(context, "RuntimeError", error.what());
  } catch (...) {
    return JS_ThrowInternalError(context, "Unexpected WebAssembly failure");
  }
}
}  // namespace
void install_wasm_bindings(JSContext* context, JSValueConst global) {
  wasm_handle_class(context);
  auto lifetime = std::make_unique<WasmHandle>();
  lifetime->kind = WasmKind::Scope;
  lifetime->scope = static_cast<ScriptContext*>(JS_GetContextOpaque(context))->wasm;
  JsValueOwner anchor(context, wasm_wrap(context, std::move(lifetime)));
  if (JS_IsException(anchor.get())) throw std::bad_alloc();
  auto value = anchor.get();
  const auto function = JS_NewCFunctionData(context, operate, 4, 0, 1, &value);
  if (JS_IsException(function) || JS_SetPropertyStr(context, global, "__webcliWasm", function) < 0)
    throw std::bad_alloc();
}
void shutdown_wasm(ScriptContext& state) {
  if (state.wasm) {
    state.wasm->active = false;
    state.wasm->state = nullptr;
  }
  state.wasm.reset();
}
}  // namespace webcli
