#pragma once

#include <string>

namespace webcli {
enum class BrowserAction { Open, Observe, Click, Fill, Select, Keyboard, Wait };
enum class WaitCondition { Idle, Text, Gone, Reference, Selector };

// Engine inputs contain browsing semantics, never terminal/IPC/output options.
struct EngineRequest {
  BrowserAction action = BrowserAction::Observe;
  std::string target, value, document;
  WaitCondition condition = WaitCondition::Idle;
  unsigned timeout_ms = 5000;
};
const char* action_name(BrowserAction);
void validate_engine_request(const EngineRequest&);
std::string normalized_ref(std::string);
}  // namespace webcli
