#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

#include "core/page.hpp"

namespace webcli {
// A compact description of an observed action, without field values.
struct ReferenceHint {
  char kind = 0;
  int frame = 0;
  std::uint64_t generation = 0;
  std::string label, url, type, role, id, name;
};
using ReferenceHints = std::unordered_map<std::string, ReferenceHint>;
ReferenceHints describe_references(const Page&);
std::optional<std::string> recover_reference(const Page&, const ReferenceHint&);
}  // namespace webcli
