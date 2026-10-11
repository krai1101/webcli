#pragma once
#include <algorithm>
#include <chrono>

#include "core/errors.hpp"
#include "core/page.hpp"

namespace webcli {
// One wall-clock deadline for an initial load or subsequent JS turn. Root
// scripts and imports share it; CPU accounting remains independent.
class ScriptLoading {
 public:
  void begin() { deadline_ = Clock::now() + std::chrono::milliseconds(kScriptLoadTimeoutMs); }
  long request_timeout() const {
    const auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline_ - Clock::now()).count();
    if (remaining <= 0)
      throw CliError("SCRIPT_LOAD_TIMEOUT", "script loading exceeded the 60-second deadline", true);
    return std::min<long>(kScriptRequestTimeoutMs, static_cast<long>(remaining));
  }

 private:
  using Clock = std::chrono::steady_clock;
  Clock::time_point deadline_ = Clock::now() + std::chrono::milliseconds(kScriptLoadTimeoutMs);
};
}  // namespace webcli
