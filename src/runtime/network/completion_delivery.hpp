#pragma once

#include <memory>

namespace webcli {
struct ScriptContext;
struct FetchJob;
// Returns false without consuming the request when this turn cannot deliver it.
bool deliver_resource_completion(ScriptContext&, FetchJob&);
void schedule_resource_completion(ScriptContext&, std::unique_ptr<FetchJob>);
}  // namespace webcli
