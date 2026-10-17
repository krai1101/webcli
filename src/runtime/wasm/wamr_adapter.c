// Isolate the few interpreter operations absent from WAMR's public C API.
// This adapter is tied to the exact WAMR revision pinned by our build.
#include "wamr_adapter.h"

#include <stdarg.h>
#include <stdio.h>

#include "wasm_c_api_internal.h"
#include "wasm_runtime.h"
#if defined(_MSC_VER)
__declspec(thread) static char last_error[1024];
#else
static __thread char last_error[1024];
#endif
void webcli_wasm_log(uint32_t level, const char* file, int line, const char* format, ...) {
  (void)file;
  (void)line;
  if (level > 2) return;
  va_list arguments;
  va_start(arguments, format);
  vsnprintf(last_error, sizeof(last_error), format, arguments);
  va_end(arguments);
}
const char* webcli_wasm_error(void) { return last_error; }
void webcli_wasm_prepare_module(wasm_module_t* module) {
  /* WAMR's C module handle starts with the underlying runtime module pointer.
     Host table.grow must be permitted even without a table.grow opcode. */
  WASMModule* loaded = *(WASMModule**)module;
  for (uint32_t i = 0; i < loaded->table_count; ++i)
    loaded->tables[i].table_type.possible_grow = true;
}

void webcli_wasm_terminate(wasm_instance_t* instance) {
  wasm_runtime_terminate(instance->inst_comm_rt);
}
void webcli_wasm_visit_references(wasm_instance_t* instance,
                                 void (*visit)(void*, wasm_ref_t*), void* data) {
  WASMModuleInstance* module = (WASMModuleInstance*)instance->inst_comm_rt;
  for (uint32_t i = 0; i < module->e->global_count; ++i) {
    WASMGlobalInstance* global = module->e->globals + i;
    if (global->type == VALUE_TYPE_EXTERNREF) {
      void* object = NULL;
      uint32_t index;
      memcpy(&index, module->global_data + global->data_offset, sizeof(index));
      if (wasm_externref_ref2obj(index, &object) && object) visit(data, object);
    }
  }
  for (uint32_t i = 0; i < module->table_count; ++i) {
    uint8_t type;
    uint32_t initial, maximum;
    wasm_runtime_get_table_inst_elem_type(instance->inst_comm_rt, i, &type, &initial, &maximum);
    if (type != VALUE_TYPE_EXTERNREF) continue;
    WASMTableInstance* table = wasm_get_table_inst(module, i);
    for (uint32_t j = 0; j < table->cur_size; ++j) {
      void* object = NULL;
      if (wasm_externref_ref2obj(table->elems[j], &object) && object) visit(data, object);
    }
  }
  wasm_externref_reclaim(instance->inst_comm_rt);
}
bool webcli_wasm_memory_grow(wasm_memory_t* memory, uint32_t pages) {
  return wasm_enlarge_memory_with_idx((WASMModuleInstance*)memory->inst_comm_rt, pages,
                                      memory->memory_idx_rt);
}
bool webcli_wasm_table_grow(wasm_table_t* table, uint32_t entries, wasm_ref_t* reference) {
  uint32_t initial = NULL_REF;
  if (reference) {
    if (table->type->val_type->kind == WASM_EXTERNREF) {
      if (!wasm_externref_obj2ref(table->inst_comm_rt, reference, &initial)) return false;
    } else {
      if (reference->inst_comm_rt != table->inst_comm_rt) return false;
      initial = reference->ref_idx_rt;
    }
  }
  return wasm_enlarge_table((WASMModuleInstance*)table->inst_comm_rt, table->table_idx_rt, entries,
                            initial);
}
bool webcli_wasm_start(wasm_instance_t* instance, uint32_t index, wasm_trap_t** trap) {
  wasm_func_t* fn =
      wasm_func_new_internal(instance->store, (uint16_t)index, instance->inst_comm_rt);
  if (!fn) return false;
  wasm_val_vec_t args = WASM_EMPTY_VEC, results = WASM_EMPTY_VEC;
  *trap = wasm_func_call(fn, &args, &results);
  wasm_func_delete(fn);
  return true;
}
