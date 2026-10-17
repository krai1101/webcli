#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>

#include "runtime/engine/js_handles.hpp"
#include "runtime/engine/script_context.hpp"
#include "runtime/wasm/objects.hpp"
#include "runtime/wasm/wamr_adapter.h"

namespace webcli {
namespace {
// One monitor per process, not one new thread for each exported function call.
class WasmMonitor {
  struct Entry {
    wasm_instance_t* instance;
    ExecutionBudget::Clock::time_point deadline;
    std::shared_ptr<std::atomic<bool>> cancelled;
    bool terminated = false;
    unsigned paused = 0;
  };
  std::mutex mutex_;
  std::condition_variable condition_;
  std::unordered_map<const void*, Entry> entries_;
  bool stop_ = false;
  std::thread thread_;

 public:
  WasmMonitor()
      : thread_([this] {
          std::unique_lock<std::mutex> lock(mutex_);
          while (!stop_) {
            if (entries_.empty()) {
              condition_.wait(lock, [this] { return stop_ || !entries_.empty(); });
              if (stop_) break;
            }
            for (auto& item : entries_) {
              auto& entry = item.second;
              if (!entry.terminated && !entry.paused &&
                  (ExecutionBudget::Clock::now() >= entry.deadline ||
                   (entry.cancelled && entry.cancelled->load()))) {
                webcli_wasm_terminate(entry.instance);
                entry.terminated = true;
              }
            }
            condition_.wait_for(lock, std::chrono::milliseconds(20));
          }
        }) {}
  ~WasmMonitor() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stop_ = true;
      condition_.notify_all();
    }
    thread_.join();
  }
  void enter(const void* id, WasmInstance& owner) {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_[id] = {owner.value, owner.scope->state->execution.interrupt_deadline(),
                    owner.scope->state->cancelled};
    condition_.notify_all();
  }
  void leave(const void* id) {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.erase(id);
    condition_.notify_all();
  }
  void refresh(WasmInstance& owner) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& item : entries_)
      if (item.second.instance == owner.value) {
        if (item.second.paused) --item.second.paused;
        item.second.deadline = owner.scope->state->execution.interrupt_deadline();
      }
  }
  void pause(WasmInstance& owner) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& item : entries_)
      if (item.second.instance == owner.value) ++item.second.paused;
  }
};
WasmMonitor& monitor() {
  static WasmMonitor value;
  return value;
}
struct Running {
  explicit Running(WasmInstance& owner) { monitor().enter(this, owner); }
  ~Running() { monitor().leave(this); }
};
struct ImportedCall {
  WasmInstance& owner;
  explicit ImportedCall(WasmInstance& instance) : owner(instance) { monitor().pause(owner); }
  ~ImportedCall() { monitor().refresh(owner); }
};
wasm_ref_t* reference(WasmInstance& owner, JSValueConst value) {
  if (JS_IsNull(value)) return nullptr;
  const auto context = owner.scope->context;
  for (const auto& existing : owner.references)
    if (existing.live && JS_IsSameValue(context, existing.value, value)) return existing.reference;
  for (auto& existing : owner.references)
    if (!existing.live) {
      existing.value = JS_DupValue(context, value);
      existing.live = true;
      return existing.reference;
    }
  owner.references.reserve(owner.references.size() + 1);
  auto* foreign = wasm_foreign_new(owner.store->value);
  if (!foreign) throw std::bad_alloc();
  auto* ref = wasm_foreign_as_ref(foreign);
  if (!ref) throw std::bad_alloc();
  owner.references.push_back({ref, JS_DupValue(context, value)});
  return ref;
}
wasm_val_t to_native(WasmInstance& owner, JSValueConst value, wasm_valkind_t kind) {
  const auto context = owner.scope->context;
  wasm_val_t result{};
  result.kind = kind;
  int converted = 0;
  switch (kind) {
    case WASM_I32:
      converted = JS_ToInt32(context, &result.of.i32, value);
      break;
    case WASM_I64:
      if (!JS_IsBigInt(value)) throw std::invalid_argument("WebAssembly i64 requires BigInt");
      converted = JS_ToBigInt64(context, &result.of.i64, value);
      break;
    case WASM_F32: {
      double number;
      converted = JS_ToFloat64(context, &number, value);
      if (converted >= 0) result.of.f32 = float(number);
      break;
    }
    case WASM_F64:
      converted = JS_ToFloat64(context, &result.of.f64, value);
      break;
    case WASM_EXTERNREF:
      result.of.ref = reference(owner, value);
      break;
    default:
      throw std::invalid_argument("Unsupported WebAssembly boundary value type");
  }
  if (converted < 0) throw std::invalid_argument("Invalid WebAssembly argument");
  return result;
}
JSValue to_js(WasmInstance& owner, const wasm_val_t& value) {
  const auto context = owner.scope->context;
  switch (value.kind) {
    case WASM_I32:
      return JS_NewInt32(context, value.of.i32);
    case WASM_I64:
      return JS_NewBigInt64(context, value.of.i64);
    case WASM_F32:
      return JS_NewFloat64(context, value.of.f32);
    case WASM_F64:
      return JS_NewFloat64(context, value.of.f64);
    case WASM_EXTERNREF:
      if (!value.of.ref) return JS_NULL;
      for (const auto& ref : owner.references)
        if (ref.live && ref.reference == value.of.ref) return JS_DupValue(context, ref.value);
      throw std::runtime_error("Unknown WebAssembly external reference");
    default:
      throw std::invalid_argument("Unsupported WebAssembly boundary value type");
  }
}
std::string trap_message(wasm_trap_t* trap) {
  const std::unique_ptr<wasm_trap_t, decltype(&wasm_trap_delete)> owned(trap, wasm_trap_delete);
  wasm_message_t message = WASM_EMPTY_VEC;
  wasm_trap_message(trap, &message);
  std::string text;
  try {
    if (message.num_elems) text.assign(message.data, message.num_elems);
  } catch (...) {
    wasm_byte_vec_delete(&message);
    throw;
  }
  if (!text.empty() && text.back() == '\0') text.pop_back();
  wasm_byte_vec_delete(&message);
  return text;
}
void* free_buffer(JSRuntime*, void* opaque, void*, size_t size) {
  if (size == 0) delete static_cast<std::shared_ptr<WasmStore>*>(opaque);
  return nullptr;
}
struct ReferenceCheckpoint {
  WasmInstance& owner;
  explicit ReferenceCheckpoint(WasmInstance& value) : owner(value) { ++owner.call_depth; }
  ~ReferenceCheckpoint() {
    --owner.call_depth;
    wasm_collect_references(owner);
  }
};
}  // namespace
void wasm_collect_references(WasmInstance& owner) noexcept {
  // Imported callbacks can reenter Wasm. Stack references are live until the
  // outermost call returns; only then are tables and globals the native roots.
  if (owner.call_depth || !owner.value) return;
  for (auto& ref : owner.references) ref.marked = false;
  webcli_wasm_visit_references(owner.value, [](void* data, wasm_ref_t* native) {
    for (auto& ref : static_cast<WasmInstance*>(data)->references)
      if (ref.reference == native) ref.marked = true;
  }, &owner);
  for (auto& ref : owner.references)
    if (ref.live && !ref.marked) {
      JS_FreeValueRT(owner.scope->runtime, ref.value);
      ref.value = JS_UNDEFINED;
      ref.live = false;
    }
}
void wasm_refresh_buffers(WasmInstance& owner) {
  for (auto& buffer : owner.buffers) {
    if (JS_IsUndefined(buffer.value)) continue;
    size_t live_size = 0;
    auto* live_data = JS_GetArrayBuffer(owner.scope->context, &live_size, buffer.value);
    const bool detached = JS_HasException(owner.scope->context);
    if (detached) JS_FreeValue(owner.scope->context, JS_GetException(owner.scope->context));
    if (!JS_IsUndefined(buffer.value) &&
        (detached || live_size != buffer.size || live_data != buffer.data ||
         buffer.data != reinterpret_cast<uint8_t*>(wasm_memory_data(buffer.memory)) ||
         buffer.size != wasm_memory_data_size(buffer.memory))) {
      JS_DetachArrayBuffer(owner.scope->context, buffer.value);
      JS_FreeValue(owner.scope->context, buffer.value);
      buffer.value = JS_UNDEFINED;
    }
  }
}
wasm_trap_t* wasm_import_call(void* opaque, const wasm_val_vec_t* args, wasm_val_vec_t* output) {
  auto& imported = *static_cast<WasmImport*>(opaque);
  auto& owner = *imported.owner;
  const auto context = owner.scope->context;
  std::vector<JSValue> values;
  ImportedCall imported_call(owner);
  try {
    wasm_check_active(owner);
    wasm_refresh_buffers(owner);
    for (size_t i = 0; i < args->num_elems; ++i) values.push_back(to_js(owner, args->data[i]));
    const auto result =
        JS_Call(context, imported.callback, JS_UNDEFINED, int(values.size()), values.data());
    for (auto value : values) JS_FreeValue(context, value);
    values.clear();
    if (JS_IsException(result)) {
      JS_FreeValue(context, owner.exception);
      owner.exception = JS_GetException(context);
      owner.has_exception = true;
      throw std::runtime_error("JavaScript import threw");
    }
    JsValueOwner returned(context, result);
    const auto* types = wasm_functype_results(imported.type);
    if (types->num_elems == 1)
      output->data[0] = to_native(owner, result, wasm_valtype_kind(types->data[0]));
    else
      for (size_t i = 0; i < types->num_elems; ++i) {
        JsValueOwner item(context, JS_GetPropertyUint32(context, result, uint32_t(i)));
        output->data[i] = to_native(owner, item.get(), wasm_valtype_kind(types->data[i]));
      }
    return nullptr;
  } catch (const std::exception& error) {
    if (JS_HasException(context) && !owner.has_exception) {
      owner.exception = JS_GetException(context);
      owner.has_exception = true;
    }
    for (auto value : values) JS_FreeValue(context, value);
    wasm_message_t message = WASM_EMPTY_VEC;
    const auto text = std::string(error.what());
    message.data = const_cast<char*>(text.data());
    message.size = message.num_elems = text.size();
    message.size_of_elem = 1;
    return wasm_trap_new(owner.store->value, &message);
  } catch (...) {
    wasm_message_t message = WASM_EMPTY_VEC;
    return wasm_trap_new(owner.store->value, &message);
  }
}
void wasm_run_start(WasmInstance& owner, uint32_t index) {
  ReferenceCheckpoint references(owner);
  Running running(owner);
  wasm_trap_t* trap = nullptr;
  if (!webcli_wasm_start(owner.value, index, &trap)) throw std::bad_alloc();
  if (trap) throw std::runtime_error(trap_message(trap));
}
JSValue wasm_call(JSContext* context, WasmHandle& handle, JSValueConst input) {
  auto& owner = *handle.instance;
  ReferenceCheckpoint references(owner);
  auto* fn = wasm_extern_as_func(handle.external);
  auto* type = wasm_func_type(fn);
  const auto* parameters = wasm_functype_params(type);
  const auto* results = wasm_functype_results(type);
  std::vector<wasm_val_t> arguments(parameters->num_elems), returned(results->num_elems);
  try {
    for (size_t i = 0; i < parameters->num_elems; ++i) {
      JsValueOwner value(context, JS_GetPropertyUint32(context, input, uint32_t(i)));
      arguments[i] = to_native(owner, value.get(), wasm_valtype_kind(parameters->data[i]));
    }
    wasm_val_vec_t params = WASM_EMPTY_VEC, output = WASM_EMPTY_VEC;
    params.data = arguments.data();
    params.size = params.num_elems = arguments.size();
    params.size_of_elem = sizeof(wasm_val_t);
    output.data = returned.data();
    output.size = output.num_elems = returned.size();
    output.size_of_elem = sizeof(wasm_val_t);
    Running running(owner);
    auto* trap = wasm_func_call(fn, &params, &output);
    wasm_refresh_buffers(owner);
    if (trap) {
      auto message = trap_message(trap);
      wasm_functype_delete(type);
      if (owner.has_exception) {
        auto exception = owner.exception;
        owner.exception = JS_UNDEFINED;
        owner.has_exception = false;
        return JS_Throw(context, exception);
      }
      return wasm_error(context, "RuntimeError", message);
    }
    wasm_functype_delete(type);
    type = nullptr;
    if (returned.empty()) return JS_UNDEFINED;
    if (returned.size() == 1) return to_js(owner, returned[0]);
    auto result = JS_NewArray(context);
    for (size_t i = 0; i < returned.size(); ++i)
      JS_SetPropertyUint32(context, result, uint32_t(i), to_js(owner, returned[i]));
    return result;
  } catch (...) {
    if (type) wasm_functype_delete(type);
    throw;
  }
}
JSValue wasm_memory_buffer(JSContext* context, WasmHandle& handle, JSValueConst) {
  auto& owner = *handle.instance;
  auto* memory = wasm_extern_as_memory(handle.external);
  wasm_refresh_buffers(owner);
  auto found = std::find_if(owner.buffers.begin(), owner.buffers.end(), [&](const auto& item) {
    return wasm_memory_same(item.memory, memory);
  });
  if (found == owner.buffers.end()) {
    owner.buffers.push_back({wasm_memory_copy(memory)});
    found = owner.buffers.end() - 1;
  }
  if (JS_IsUndefined(found->value)) {
    found->data = reinterpret_cast<uint8_t*>(wasm_memory_data(memory));
    found->size = wasm_memory_data_size(memory);
    auto* retained = new std::shared_ptr<WasmStore>(owner.store);
    found->value =
        JS_NewArrayBuffer(context, found->data, found->size, 0, free_buffer, retained, false);
    if (JS_IsException(found->value)) {
      delete retained;
      found->value = JS_UNDEFINED;
      return JS_EXCEPTION;
    }
    // This non-configurable edge makes imports visible to QuickJS's cycle collector.
    JS_DefinePropertyValueStr(context, found->value, "__wasmOwner",
                              JS_DupValue(context, handle.anchor), 0);
  }
  return JS_DupValue(context, found->value);
}
JSValue wasm_memory_grow(JSContext* context, WasmHandle& handle, uint32_t pages) {
  auto* memory = wasm_extern_as_memory(handle.external);
  const auto previous = wasm_memory_size(memory);
  if (!webcli_wasm_memory_grow(memory, pages))
    return JS_ThrowRangeError(context, "WebAssembly memory cannot grow");
  // Even grow(0) invalidates a non-shared buffer.
  for (auto& buffer : handle.instance->buffers)
    if (wasm_memory_same(buffer.memory, memory) && !JS_IsUndefined(buffer.value)) {
      JS_DetachArrayBuffer(context, buffer.value);
      JS_FreeValue(context, buffer.value);
      buffer.value = JS_UNDEFINED;
    }
  return JS_NewUint32(context, previous);
}
JSValue wasm_global_get(JSContext*, WasmHandle& handle) {
  wasm_val_t value{};
  wasm_global_get(wasm_extern_as_global(handle.external), &value);
  return to_js(*handle.instance, value);
}
JSValue wasm_global_set(JSContext* context, WasmHandle& handle, JSValueConst input) {
  ReferenceCheckpoint references(*handle.instance);
  auto* global = wasm_extern_as_global(handle.external);
  auto* type = wasm_global_type(global);
  const auto mutable_value = wasm_globaltype_mutability(type) == WASM_VAR;
  const auto kind = wasm_valtype_kind(wasm_globaltype_content(type));
  wasm_globaltype_delete(type);
  if (!mutable_value) return JS_ThrowTypeError(context, "WebAssembly global is immutable");
  const auto value = to_native(*handle.instance, input, kind);
  wasm_global_set(global, &value);
  return JS_UNDEFINED;
}
JSValue wasm_table_access(JSContext* context, WasmHandle& handle, const std::string& action,
                          uint32_t index, JSValueConst input, JSValueConst) {
  auto* table = wasm_extern_as_table(handle.external);
  auto& owner = *handle.instance;
  ReferenceCheckpoint references(owner);
  const auto length = wasm_table_size(table);
  if (action == "length") return JS_NewUint32(context, length);
  auto* type = wasm_table_type(table);
  const auto kind = wasm_valtype_kind(wasm_tabletype_element(type));
  wasm_tabletype_delete(type);
  if (action == "tableType")
    return JS_NewString(context, kind == WASM_EXTERNREF ? "externref" : "funcref");
  if (action != "grow" && index >= length)
    return JS_ThrowRangeError(context, "WebAssembly table index out of bounds");
  if (action == "set" || action == "grow") {
    wasm_ref_t* ref = nullptr;
    if (kind == WASM_EXTERNREF)
      ref = reference(owner, input);
    else if (!JS_IsNull(input)) {
      auto& fn = wasm_handle(context, input, WasmKind::Function);
      if (fn.instance != handle.instance)
        return JS_ThrowTypeError(context, "Cross-instance function references are unsupported");
      ref = wasm_func_as_ref(wasm_extern_as_func(fn.external));
    }
    if (action == "grow") {
      const auto grown = webcli_wasm_table_grow(table, index, ref);
      if (kind == WASM_FUNCREF && ref) wasm_ref_delete(ref);
      if (!grown) return JS_ThrowRangeError(context, "WebAssembly table cannot grow");
      return JS_NewUint32(context, length);
    }
    if (!wasm_table_set(table, index, ref)) {
      if (kind == WASM_FUNCREF && ref) wasm_ref_delete(ref);
      return JS_ThrowTypeError(context, "Invalid table value");
    }
    return JS_UNDEFINED;
  }
  auto* ref = wasm_table_get(table, index);
  if (!ref) return JS_NULL;
  if (kind == WASM_EXTERNREF) {
    wasm_val_t value{};
    value.kind = WASM_EXTERNREF;
    value.of.ref = ref;
    return to_js(owner, value);
  }
  auto* fn = wasm_ref_as_func(ref);
  wasm_ref_delete(ref);
  if (!fn) return JS_ThrowTypeError(context, "Invalid function reference");
  auto wrapped = std::make_unique<WasmHandle>();
  wrapped->kind = WasmKind::Function;
  wrapped->instance = handle.instance;
  wrapped->external = wasm_func_as_extern(fn);
  wrapped->anchor = JS_DupValue(context, handle.anchor);
  return wasm_function(context, std::move(wrapped));
}
}  // namespace webcli
