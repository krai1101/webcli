#pragma once
#include <cstddef>

#include "core/errors.hpp"
#include "core/resource_limits.hpp"

namespace webcli {
// Cumulative allocation accounting includes detached nodes retained by Lexbor.
// Reservations happen before native allocation; rejected reservations change nothing.
class DomAllocationBudget {
 public:
  static constexpr std::size_t max_bytes = kDomAllocationBytes;
  static constexpr std::size_t max_nodes = kDomNodes;
  static constexpr std::size_t max_fragment_bytes = kDomFragmentBytes;
  std::size_t remaining_bytes() const { return max_bytes - bytes_; }

  void reserve(std::size_t bytes, std::size_t nodes = 0) {
    if (bytes > max_bytes - bytes_)
      throw CliError("DOM_QUOTA", "DOM allocation byte quota exceeded");
    if (nodes > max_nodes - nodes_)
      throw CliError("DOM_QUOTA", "DOM allocation node quota exceeded");
    bytes_ += bytes;
    nodes_ += nodes;
  }
  // Attribute replacement frees the previous value buffer in Lexbor. Detached
  // nodes still remain charged; this releases only proven freed string storage.
  void release_bytes(std::size_t bytes) { bytes_ -= bytes > bytes_ ? bytes_ : bytes; }
  // A collected detached document has destroyed its entire native allocation.
  void release_tree(std::size_t bytes, std::size_t nodes) {
    release_bytes(bytes);
    nodes_ -= nodes > nodes_ ? nodes_ : nodes;
  }
  // Parsing allocates through Lexbor directly. Reconcile its live tree before
  // exposing parser-created nodes to bindings or allowing further allocations.
  void observe_live_usage(std::size_t bytes, std::size_t nodes) {
    if (bytes > max_bytes || nodes > max_nodes)
      throw CliError("DOM_QUOTA", "parser-created DOM exceeds allocation limits");
    if (bytes > bytes_) bytes_ = bytes;
    if (nodes > nodes_) nodes_ = nodes;
  }

 private:
  std::size_t bytes_ = 0;
  std::size_t nodes_ = 0;
};
}  // namespace webcli
