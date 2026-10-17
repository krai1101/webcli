#pragma once
#include <wasm_c_api.h>

#include <atomic>
#include <cstddef>
#include <memory>
namespace webcli {
struct WasmAccount {
  std::atomic<size_t> used{0};
  size_t limit;
  explicit WasmAccount(size_t bytes) : limit(bytes) {}
};
class WasmAllocation {
  std::shared_ptr<WasmAccount> previous_;

 public:
  explicit WasmAllocation(std::shared_ptr<WasmAccount>);
  ~WasmAllocation();
};
wasm_engine_t* wasm_engine();
}  // namespace webcli
