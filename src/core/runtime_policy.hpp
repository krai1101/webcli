#pragma once
#include <cstddef>
#include <string>

namespace webcli {
struct RuntimePolicy {
  bool block_private_networks = false;
  std::size_t js_heap_bytes = 0;  // Zero keeps the JavaScript engine's default.
  std::string identity() const;
};
// Read once before spawning workers; immutable for the lifetime of a process.
const RuntimePolicy& runtime_policy();
}  // namespace webcli
