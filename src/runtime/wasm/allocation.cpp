#include "runtime/wasm/allocation.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>

namespace webcli {
namespace {
thread_local std::shared_ptr<WasmAccount> current;
struct alignas(std::max_align_t) Allocation {
  size_t size;
  std::shared_ptr<WasmAccount> account;
};
bool reserve(const std::shared_ptr<WasmAccount>& account, size_t size) {
  if (!account) return true;
  auto previous = account->used.load();
  do {
    if (size > SIZE_MAX - previous ||
        (account->limit && size > account->limit - std::min(previous, account->limit)))
      return false;
  } while (!account->used.compare_exchange_weak(previous, previous + size));
  return true;
}
void* allocate(unsigned int size) noexcept {
  if (!reserve(current, size)) return nullptr;
  auto* storage = std::malloc(sizeof(Allocation) + size);
  if (!storage) {
    if (current) current->used.fetch_sub(size);
    return nullptr;
  }
  auto* header = new (storage) Allocation{size, current};
  return header + 1;
}
void release(void* pointer) noexcept {
  if (!pointer) return;
  auto* header = static_cast<Allocation*>(pointer) - 1;
  if (header->account) header->account->used.fetch_sub(header->size);
  header->~Allocation();
  std::free(header);
}
void* resize(void* pointer, unsigned int size) noexcept {
  if (!pointer) return allocate(size);
  if (!size) {
    release(pointer);
    return nullptr;
  }
  auto* header = static_cast<Allocation*>(pointer) - 1;
  const auto old = header->size;
  auto account = header->account;
  if (size > old && !reserve(account, size - old)) return nullptr;
  // shared_ptr is not trivially relocatable, so move the payload explicitly.
  auto* storage = std::malloc(sizeof(Allocation) + size);
  if (!storage) {
    if (account && size > old) account->used.fetch_sub(size - old);
    return nullptr;
  }
  auto* replacement = new (storage) Allocation{size, account};
  std::memcpy(static_cast<void*>(replacement + 1), pointer, std::min<size_t>(old, size));
  header->~Allocation();
  std::free(header);
  if (account && size < old) account->used.fetch_sub(old - size);
  return replacement + 1;
}
}  // namespace
WasmAllocation::WasmAllocation(std::shared_ptr<WasmAccount> account) : previous_(current) {
  current = std::move(account);
}
WasmAllocation::~WasmAllocation() { current = std::move(previous_); }
wasm_engine_t* wasm_engine() {
  static std::mutex mutex;
  static wasm_engine_t* engine = nullptr;
  std::lock_guard<std::mutex> lock(mutex);
  if (!engine) {
    WasmAllocation global(nullptr);
    MemAllocOption options{};
    options.allocator.malloc_func = reinterpret_cast<void*>(allocate);
    options.allocator.realloc_func = reinterpret_cast<void*>(resize);
    options.allocator.free_func = reinterpret_cast<void*>(release);
    auto* config = wasm_config_new();
    if (!config) throw std::bad_alloc();
    wasm_config_set_mem_alloc_opt(config, Alloc_With_Allocator, &options);
    engine = wasm_engine_new_with_config(config);
    wasm_config_delete(config);
  }
  if (!engine) throw std::bad_alloc();
  return engine;
}
}  // namespace webcli
