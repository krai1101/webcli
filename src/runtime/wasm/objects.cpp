#include "runtime/wasm/objects.hpp"

#include <algorithm>
#include <stdexcept>

#include "core/resource_limits.hpp"
#include "core/runtime_policy.hpp"
#include "runtime/engine/js_handles.hpp"
#include "runtime/engine/script_context.hpp"
#include "runtime/wasm/wamr_adapter.h"

namespace webcli {
namespace {
void finalize(JSRuntime* runtime, JSValue value) {
  auto* handle = static_cast<WasmHandle*>(JS_GetOpaque(value, JS_GetClassID(value)));
  if (!handle) return;
  (void)runtime;
  delete handle;
}
void mark(JSRuntime* runtime, JSValueConst value, JS_MarkFunc* visit) {
  auto* handle = static_cast<WasmHandle*>(JS_GetOpaque(value, JS_GetClassID(value)));
  if (!handle) return;
  JS_MarkValue(runtime, handle->anchor, visit);
  if (handle->kind != WasmKind::Instance) return;
  const auto& owner = *handle->instance;
  JS_MarkValue(runtime, owner.exception, visit);
  for (const auto& imported : owner.imports) JS_MarkValue(runtime, imported->callback, visit);
  for (const auto& buffer : owner.buffers) JS_MarkValue(runtime, buffer.value, visit);
  for (const auto& ref : owner.references) JS_MarkValue(runtime, ref.value, visit);
  for (auto fn : owner.functions) JS_MarkValue(runtime, fn, visit);
}
uint32_t leb(const std::vector<uint8_t>& bytes, size_t& position) {
  uint32_t result = 0;
  for (unsigned i = 0; i < 5 && position < bytes.size(); ++i) {
    const auto byte = bytes[position++];
    if (i == 4 && (byte & 0xf0)) throw std::runtime_error("Invalid section length");
    result |= uint32_t(byte & 127) << (i * 7);
    if (!(byte & 128)) return result;
  }
  throw std::runtime_error("Invalid section length");
}
wasm_byte_vec_t view(const std::vector<uint8_t>& bytes) {
  wasm_byte_vec_t result = WASM_EMPTY_VEC;
  result.data = reinterpret_cast<char*>(const_cast<uint8_t*>(bytes.data()));
  result.size = result.num_elems = bytes.size();
  result.size_of_elem = 1;
  return result;
}
std::vector<uint8_t> without_start(const WasmHandle& source) {
  auto result = source.bytes;
  for (size_t position = 8; position < result.size();) {
    const auto beginning = position;
    const auto id = result[position++];
    const auto length = leb(result, position);
    if (length > result.size() - position) throw std::runtime_error("Invalid section length");
    if (id == 8) {
      result.erase(result.begin() + beginning, result.begin() + position + length);
      break;
    }
    position += length;
  }
  return result;
}
const char* kind_name(wasm_externkind_t kind) {
  switch (kind) {
    case WASM_EXTERN_FUNC:
      return "function";
    case WASM_EXTERN_MEMORY:
      return "memory";
    case WASM_EXTERN_GLOBAL:
      return "global";
    case WASM_EXTERN_TABLE:
      return "table";
    default:
      return "unknown";
  }
}
}  // namespace

JSClassID wasm_handle_class(JSContext* context) {
  auto* state = static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  if (!state->wasm) state->wasm = std::make_shared<WasmScope>(state, context, state->runtime);
  auto& id = state->wasm->handle_class;
  if (!state->wasm->account)
    state->wasm->account = std::make_shared<WasmAccount>(runtime_policy().js_heap_bytes);
  if (!id) JS_NewClassID(state->runtime, &id);
  return id;
}
WasmStore::WasmStore() {
  // WAMR requires one process-wide engine; stores belong to their creating thread.
  if (!(value = wasm_store_new(wasm_engine()))) throw std::bad_alloc();
}
WasmStore::~WasmStore() {
  if (value) wasm_store_delete(value);
}
WasmImport::~WasmImport() {
  JS_FreeValueRT(owner->scope->runtime, callback);
  if (type) wasm_functype_delete(type);
}
WasmInstance::~WasmInstance() {
  for (auto fn : functions) JS_FreeValueRT(scope->runtime, fn);
  for (auto& buffer : buffers) {
    JS_FreeValueRT(scope->runtime, buffer.value);
    wasm_memory_delete(buffer.memory);
  }
  JS_FreeValueRT(scope->runtime, exception);
  for (auto& ref : references) {
    JS_FreeValueRT(scope->runtime, ref.value);
    wasm_ref_delete(ref.reference);
  }
  for (auto* imported : native_imports) wasm_extern_delete(imported);
}
WasmHandle::~WasmHandle() {
  if (external) wasm_extern_delete(external);
  if (instance) JS_FreeValueRT(instance->scope->runtime, anchor);
}
WasmHandle& wasm_handle(JSContext* context, JSValueConst value, WasmKind kind) {
  auto* handle = static_cast<WasmHandle*>(JS_GetOpaque(value, wasm_handle_class(context)));
  if (!handle || handle->kind != kind) throw std::invalid_argument("Invalid WebAssembly receiver");
  if (handle->instance) wasm_check_active(*handle->instance);
  return *handle;
}
JSValue wasm_wrap(JSContext* context, std::unique_ptr<WasmHandle> handle) {
  const auto id = wasm_handle_class(context);
  if (!JS_IsRegisteredClass(JS_GetRuntime(context), id)) {
    JSClassDef definition{};
    definition.class_name = "WebAssembly native object";
    definition.finalizer = finalize;
    definition.gc_mark = mark;
    if (JS_NewClass(JS_GetRuntime(context), id, &definition) < 0) return JS_EXCEPTION;
  }
  const auto result = JS_NewObjectClass(context, id);
  if (JS_IsException(result)) return result;
  JS_SetOpaque(result, handle.release());
  return result;
}
JSValue wasm_error(JSContext* context, const char* name, const std::string& message) {
  const auto error = JS_NewError(context);
  JS_SetPropertyStr(context, error, "name", JS_NewString(context, name));
  JS_SetPropertyStr(context, error, "message",
                    JS_NewStringLen(context, message.data(), message.size()));
  return JS_Throw(context, error);
}
std::string wasm_name(const wasm_name_t* name) { return {name->data, name->num_elems}; }
void wasm_check_active(const WasmInstance& instance) {
  if (!instance.scope->active || instance.scope->state->execution.cancelled())
    throw std::runtime_error("WebAssembly realm is closed");
  instance.scope->state->require_owner();
}
std::vector<uint8_t> wasm_bytes(JSContext* context, JSValueConst input) {
  size_t size = 0;
  auto* data = JS_GetArrayBuffer(context, &size, input);
  if (!JS_IsArrayBuffer(input)) {
    JS_FreeValue(context, JS_GetException(context));
    size_t offset = 0, element = 0;
    const auto value = JS_GetTypedArrayBuffer(context, input, &offset, &size, &element);
    if (JS_IsException(value)) throw std::invalid_argument("WebAssembly requires a BufferSource");
    size_t total = 0;
    auto* base = JS_GetArrayBuffer(context, &total, value);
    if (!base || offset > total || size > total - offset) {
      JS_FreeValue(context, value);
      throw std::invalid_argument("Invalid WebAssembly buffer");
    }
    if (size > kBufferedBodyBytes) {
      JS_FreeValue(context, value);
      throw std::length_error("WebAssembly module exceeds body limit");
    }
    std::vector<uint8_t> result;
    if (size) result.assign(base + offset, base + offset + size);
    JS_FreeValue(context, value);
    return result;
  }
  if (size > kBufferedBodyBytes) throw std::length_error("WebAssembly module exceeds body limit");
  if (!size) return {};
  return {data, data + size};
}
JSValue wasm_compile(JSContext* context, JSValueConst input) {
  auto handle = std::make_unique<WasmHandle>();
  handle->kind = WasmKind::Module;
  handle->bytes = wasm_bytes(context, input);
  handle->store = std::make_shared<WasmStore>();
  auto bytes = view(handle->bytes);
  if (!(handle->module = wasm_module_new(handle->store->value, &bytes)))
    return wasm_error(context, "CompileError", webcli_wasm_error());
  for (size_t position = 8; position < handle->bytes.size();) {
    const auto id = handle->bytes[position++];
    const auto length = leb(handle->bytes, position);
    if (length > handle->bytes.size() - position)
      throw std::runtime_error("Invalid section length");
    if (id == 8) {
      auto start = position;
      handle->start = leb(handle->bytes, start);
    }
    position += length;
  }
  return wasm_wrap(context, std::move(handle));
}
JSValue wasm_describe(JSContext* context, WasmHandle& handle, bool imports) {
  auto result = JS_NewArray(context);
  if (imports) {
    wasm_importtype_vec_t types = WASM_EMPTY_VEC;
    wasm_module_imports(handle.module, &types);
    for (size_t i = 0; i < types.num_elems; ++i) {
      auto item = JS_NewObject(context);
      const auto module = wasm_name(wasm_importtype_module(types.data[i]));
      const auto name = wasm_name(wasm_importtype_name(types.data[i]));
      JS_SetPropertyStr(context, item, "module", JS_NewString(context, module.c_str()));
      JS_SetPropertyStr(context, item, "name", JS_NewString(context, name.c_str()));
      JS_SetPropertyStr(
          context, item, "kind",
          JS_NewString(context,
                       kind_name(wasm_externtype_kind(wasm_importtype_type(types.data[i])))));
      JS_SetPropertyUint32(context, result, uint32_t(i), item);
    }
    wasm_importtype_vec_delete(&types);
  } else {
    wasm_exporttype_vec_t types = WASM_EMPTY_VEC;
    wasm_module_exports(handle.module, &types);
    for (size_t i = 0; i < types.num_elems; ++i) {
      auto item = JS_NewObject(context);
      const auto name = wasm_name(wasm_exporttype_name(types.data[i]));
      JS_SetPropertyStr(context, item, "name", JS_NewString(context, name.c_str()));
      JS_SetPropertyStr(
          context, item, "kind",
          JS_NewString(context,
                       kind_name(wasm_externtype_kind(wasm_exporttype_type(types.data[i])))));
      JS_SetPropertyUint32(context, result, uint32_t(i), item);
    }
    wasm_exporttype_vec_delete(&types);
  }
  return result;
}
JSValue wasm_instantiate(JSContext* context, WasmHandle& module, JSValueConst input) {
  auto* state = static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  if (!state->wasm) state->wasm = std::make_shared<WasmScope>(state, context, state->runtime);
  auto owner = std::make_shared<WasmInstance>();
  owner->scope = state->wasm;
  owner->store = std::make_shared<WasmStore>();
  const auto binary = without_start(module);
  auto binary_view = view(binary);
  auto* compiled = wasm_module_new(owner->store->value, &binary_view);
  if (!compiled) return wasm_error(context, "CompileError", "Cannot load WebAssembly module");
  webcli_wasm_prepare_module(compiled);
  wasm_importtype_vec_t types = WASM_EMPTY_VEC;
  wasm_module_imports(compiled, &types);
  owner->native_imports.reserve(types.num_elems);
  owner->imports.reserve(types.num_elems);
  for (size_t i = 0; i < types.num_elems; ++i) {
    const auto* description = types.data[i];
    const auto name = wasm_name(wasm_importtype_name(description));
    const auto module_name = wasm_name(wasm_importtype_module(description));
    const auto* type = wasm_importtype_type(description);
    if (wasm_externtype_kind(type) != WASM_EXTERN_FUNC) {
      wasm_importtype_vec_delete(&types);
      return wasm_error(context, "LinkError", "Memory, table and global imports are unsupported");
    }
    JsValueOwner group(context, JS_GetPropertyStr(context, input, module_name.c_str()));
    if (JS_IsException(group.get())) {
      wasm_importtype_vec_delete(&types);
      return JS_EXCEPTION;
    }
    if (!JS_IsObject(group.get())) {
      wasm_importtype_vec_delete(&types);
      return wasm_error(context, "LinkError", "Missing WebAssembly import namespace");
    }
    JsValueOwner imported(context, JS_GetPropertyStr(context, group.get(), name.c_str()));
    if (JS_IsException(imported.get())) {
      wasm_importtype_vec_delete(&types);
      return JS_EXCEPTION;
    }
    if (!JS_IsFunction(context, imported.get())) {
      wasm_importtype_vec_delete(&types);
      return wasm_error(context, "LinkError", "Missing WebAssembly function import");
    }
    auto callback = std::make_unique<WasmImport>();
    callback->owner = owner.get();
    callback->callback = JS_DupValue(context, imported.get());
    callback->type = wasm_functype_copy(wasm_externtype_as_functype_const(type));
    if (!callback->type) {
      wasm_importtype_vec_delete(&types);
      throw std::bad_alloc();
    }
    auto* fn = wasm_func_new_with_env(owner->store->value, callback->type, wasm_import_call,
                                      callback.get(), nullptr);
    if (!fn) {
      wasm_importtype_vec_delete(&types);
      throw std::bad_alloc();
    }
    owner->native_imports.push_back(wasm_func_as_extern(fn));
    owner->imports.push_back(std::move(callback));
  }
  wasm_importtype_vec_delete(&types);
  wasm_extern_vec_t imported = WASM_EMPTY_VEC;
  imported.data = owner->native_imports.data();
  imported.size = imported.num_elems = owner->native_imports.size();
  imported.size_of_elem = sizeof(wasm_extern_t*);
  wasm_trap_t* trap = nullptr;
  const auto pages = runtime_policy().js_heap_bytes
                         ? std::min<size_t>(65536, runtime_policy().js_heap_bytes / 65536)
                         : 65536;
  // Retain WAMR's 32 KiB operand-stack default; no host-managed application heap.
  InstantiationArgs args{32 * 1024, 0, uint32_t(pages)};
  owner->value =
      wasm_instance_new_with_args_ex(owner->store->value, compiled, &imported, &trap, &args);
  if (!owner->value) {
    if (trap) wasm_trap_delete(trap);
    return wasm_error(context, "LinkError", "WebAssembly instantiation failed");
  }
  if (module.start) {
    try {
      wasm_run_start(*owner, *module.start);
    } catch (...) {
      if (owner->has_exception) {
        const auto exception = owner->exception;
        owner->exception = JS_UNDEFINED;
        owner->has_exception = false;
        return JS_Throw(context, exception);
      }
      throw;
    }
  }
  auto handle = std::make_unique<WasmHandle>();
  handle->kind = WasmKind::Instance;
  handle->instance = std::move(owner);
  return wasm_wrap(context, std::move(handle));
}
JSValue wasm_function(JSContext* context, std::unique_ptr<WasmHandle> handle) {
  auto& owner = *handle->instance;
  for (auto value : owner.functions) {
    auto& existing = wasm_handle(context, value, WasmKind::Function);
    if (wasm_func_same(wasm_extern_as_func(existing.external),
                       wasm_extern_as_func(handle->external)))
      return JS_DupValue(context, value);
  }
  owner.functions.reserve(owner.functions.size() + 1);
  auto value = wasm_wrap(context, std::move(handle));
  if (!JS_IsException(value)) owner.functions.push_back(JS_DupValue(context, value));
  return value;
}
JSValue wasm_exports(JSContext* context, WasmHandle& handle, JSValueConst anchor) {
  wasm_extern_vec_t exports = WASM_EMPTY_VEC;
  wasm_instance_exports(handle.instance->value, &exports);
  auto result = JS_NewArray(context);
  for (size_t i = 0; i < exports.num_elems; ++i) {
    auto native = std::make_unique<WasmHandle>();
    native->instance = handle.instance;
    native->anchor = JS_DupValue(context, anchor);
    native->external = exports.data[i];
    exports.data[i] = nullptr;
    switch (wasm_extern_kind(native->external)) {
      case WASM_EXTERN_FUNC:
        native->kind = WasmKind::Function;
        break;
      case WASM_EXTERN_MEMORY:
        native->kind = WasmKind::Memory;
        break;
      case WASM_EXTERN_GLOBAL:
        native->kind = WasmKind::Global;
        break;
      case WASM_EXTERN_TABLE:
        native->kind = WasmKind::Table;
        break;
      default:
        continue;
    }
    const bool function = native->kind == WasmKind::Function;
    JS_SetPropertyUint32(context, result, uint32_t(i),
        function ? wasm_function(context, std::move(native)) : wasm_wrap(context, std::move(native)));
  }
  wasm_extern_vec_delete(&exports);
  return result;
}
}  // namespace webcli
