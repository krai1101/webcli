#include "runtime/scripts/module_contexts.hpp"

#include <utility>

#include "core/errors.hpp"

namespace webcli {
namespace {
constexpr std::size_t max_bytes = 8 * 1024 * 1024;
std::size_t charge(const ModuleContext& value) {
  return sizeof(value) + value.base_url.size() + value.request.source.size() +
         value.request.policy.size();
}
}  // namespace
const ModuleContext* ModuleContexts::find(const std::string& id) const {
  const auto entry = entries_.find(id);
  return entry == entries_.end() ? nullptr : &entry->second;
}
void ModuleContexts::remember(const std::string& id, std::string base, NavigationReferrer request,
                              bool module) {
  if (entries_.count(id)) return;
  ModuleContext value{std::move(base), std::move(request), module};
  const auto size = charge(value) + id.size();
  if (size > max_bytes - bytes_)
    throw CliError("SCRIPT_QUOTA", "script environment metadata exceeds document limits");
  entries_.emplace(id, std::move(value));
  bytes_ += size;
}
void ModuleContexts::received(const std::string& id, const std::string& final_url,
                              const std::string& policy) {
  const auto entry = entries_.find(id);
  if (entry == entries_.end()) return;
  auto next = entry->second;
  next.base_url = final_url;
  if (next.module) next.request.policy = response_referrer_policy(policy, next.request.policy);
  const auto previous = charge(entry->second), size = charge(next);
  if (size > max_bytes - (bytes_ - previous))
    throw CliError("SCRIPT_QUOTA", "script environment metadata exceeds document limits");
  entry->second = std::move(next);
  bytes_ = bytes_ - previous + size;
}
void ModuleContexts::dispatched(const std::string& id) {
  const auto entry = entries_.find(id);
  if (entry == entries_.end()) return;
  bytes_ -= entry->second.request.source.size();
  std::string{}.swap(entry->second.request.source);
}
}  // namespace webcli
