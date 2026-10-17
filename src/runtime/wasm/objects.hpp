#pragma once
#include <quickjs.h>
#include <wasm_c_api.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "runtime/wasm/allocation.hpp"

namespace webcli {
struct ScriptContext;
struct WasmScope {
  ScriptContext* state;
  JSContext* context;
  JSRuntime* runtime;
  bool active = true;
  JSClassID handle_class = 0;
  std::shared_ptr<WasmAccount> account;
  WasmScope(ScriptContext* owner, JSContext* realm, JSRuntime* engine)
      : state(owner), context(realm), runtime(engine) {}
};
struct WasmStore {
  wasm_store_t* value = nullptr;
  WasmStore();
  ~WasmStore();
  WasmStore(const WasmStore&) = delete;
  WasmStore& operator=(const WasmStore&) = delete;
};
struct WasmImport;
struct WasmBuffer {
  wasm_memory_t* memory;
  JSValue value = JS_UNDEFINED;
  uint8_t* data = nullptr;
  size_t size = 0;
};
struct WasmReference {
  wasm_ref_t* reference;
  JSValue value;
  bool live = true, marked = false;
};
struct WasmInstance {
  std::shared_ptr<WasmScope> scope;
  std::shared_ptr<WasmStore> store;
  wasm_instance_t* value = nullptr;
  std::vector<std::unique_ptr<WasmImport>> imports;
  std::vector<wasm_extern_t*> native_imports;
  std::vector<WasmBuffer> buffers;
  std::vector<WasmReference> references;
  unsigned call_depth = 0;
  std::vector<JSValue> functions;
  JSValue exception = JS_UNDEFINED;
  bool has_exception = false;
  ~WasmInstance();
};
struct WasmImport {
  WasmInstance* owner;
  JSValue callback = JS_UNDEFINED;
  wasm_functype_t* type = nullptr;
  ~WasmImport();
};
enum class WasmKind { Scope, Module, Instance, Function, Memory, Global, Table };
struct WasmHandle {
  WasmKind kind;
  std::shared_ptr<WasmScope> scope;
  std::shared_ptr<WasmStore> store;
  std::shared_ptr<WasmInstance> instance;
  wasm_module_t* module = nullptr;
  wasm_extern_t* external = nullptr;
  std::vector<uint8_t> bytes;
  std::optional<uint32_t> start;
  JSValue anchor = JS_UNDEFINED;
  ~WasmHandle();
  WasmHandle() = default;
  WasmHandle(const WasmHandle&) = delete;
  WasmHandle& operator=(const WasmHandle&) = delete;
};
JSClassID wasm_handle_class(JSContext*);
WasmHandle& wasm_handle(JSContext*, JSValueConst, WasmKind);
JSValue wasm_wrap(JSContext*, std::unique_ptr<WasmHandle>);
JSValue wasm_error(JSContext*, const char*, const std::string&);
std::string wasm_name(const wasm_name_t*);
std::vector<uint8_t> wasm_bytes(JSContext*, JSValueConst);
void wasm_check_active(const WasmInstance&);
JSValue wasm_compile(JSContext*, JSValueConst);
JSValue wasm_describe(JSContext*, WasmHandle&, bool);
JSValue wasm_instantiate(JSContext*, WasmHandle&, JSValueConst);
JSValue wasm_exports(JSContext*, WasmHandle&, JSValueConst);
JSValue wasm_function(JSContext*, std::unique_ptr<WasmHandle>);
void wasm_collect_references(WasmInstance&) noexcept;
JSValue wasm_call(JSContext*, WasmHandle&, JSValueConst);
JSValue wasm_memory_buffer(JSContext*, WasmHandle&, JSValueConst);
JSValue wasm_memory_grow(JSContext*, WasmHandle&, uint32_t);
JSValue wasm_global_get(JSContext*, WasmHandle&);
JSValue wasm_global_set(JSContext*, WasmHandle&, JSValueConst);
JSValue wasm_table_access(JSContext*, WasmHandle&, const std::string&, uint32_t, JSValueConst,
                          JSValueConst);
wasm_trap_t* wasm_import_call(void*, const wasm_val_vec_t*, wasm_val_vec_t*);
void wasm_run_start(WasmInstance&, uint32_t);
void wasm_refresh_buffers(WasmInstance&);
}  // namespace webcli
