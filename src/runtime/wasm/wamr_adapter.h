#pragma once
#include <wasm_c_api.h>
#ifdef __cplusplus
extern "C" {
#endif
void webcli_wasm_terminate(wasm_instance_t*);
bool webcli_wasm_memory_grow(wasm_memory_t*, uint32_t);
bool webcli_wasm_table_grow(wasm_table_t*, uint32_t, wasm_ref_t*);
bool webcli_wasm_start(wasm_instance_t*, uint32_t, wasm_trap_t**);
const char* webcli_wasm_error(void);
void webcli_wasm_prepare_module(wasm_module_t*);
void webcli_wasm_visit_references(wasm_instance_t*, void (*)(void*, wasm_ref_t*), void*);
#ifdef __cplusplus
}
#endif
