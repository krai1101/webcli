#pragma once

#include <chrono>
#include <string>

#include "core/errors.hpp"
#include "runtime/page/engine_request.hpp"

namespace webcli {
// A condition operation advances without sleeping or holding the IPC thread.
struct WaitOperation {
  using Clock = std::chrono::steady_clock;
  WaitCondition mode;
  std::string value;
  Clock::time_point deadline, idle_since = Clock::now();
  explicit WaitOperation(const EngineRequest& request)
      : mode(request.condition),
        value(request.value),
        deadline(Clock::now() + std::chrono::milliseconds(request.timeout_ms)) {
    validate_engine_request(request);
  }
};
}  // namespace webcli
