#include "runtime/page/engine_request.hpp"

#include <algorithm>

#include "core/errors.hpp"

namespace webcli {
const char* action_name(BrowserAction action) {
  switch (action) {
    case BrowserAction::Open:
      return "open";
    case BrowserAction::Observe:
      return "observe";
    case BrowserAction::Click:
      return "click";
    case BrowserAction::Fill:
      return "fill";
    case BrowserAction::Select:
      return "select";
    case BrowserAction::Keyboard:
      return "keyboard";
    case BrowserAction::Wait:
      return "wait";
    case BrowserAction::Back:
      return "back";
    case BrowserAction::Forward:
      return "forward";
  }
  throw CliError("UNKNOWN_COMMAND", "invalid browsing action");
}
void validate_engine_request(const EngineRequest& request) {
  action_name(request.action);
  if (request.timeout_ms == 0 || request.timeout_ms > 30000)
    throw CliError("INVALID_ARGUMENT", "timeout must be 1..30000 ms");
  const bool history =
      request.action == BrowserAction::Back || request.action == BrowserAction::Forward;
  if (history && (!request.target.empty() || !request.value.empty()))
    throw CliError("INVALID_ARGUMENT", "back/forward take no target or value");
  if (request.action != BrowserAction::Observe && request.action != BrowserAction::Wait &&
      !history && request.target.empty())
    throw CliError("INVALID_ARGUMENT", "browsing action requires a target");
  if (request.action == BrowserAction::Keyboard && request.value != "Enter" &&
      request.value != "Escape" && request.value != "Tab" && request.value != "Shift+Tab")
    throw CliError("INVALID_KEY", "keyboard supports Enter, Escape, Tab and Shift+Tab");
  if (request.action == BrowserAction::Wait) {
    switch (request.condition) {
      case WaitCondition::Idle:
        if (!request.value.empty()) throw CliError("INVALID_ARGUMENT", "idle wait takes no value");
        break;
      case WaitCondition::Text:
      case WaitCondition::Gone:
      case WaitCondition::Reference:
      case WaitCondition::Selector:
        if (request.value.empty())
          throw CliError("INVALID_ARGUMENT", "wait condition requires a value");
        break;
      default:
        throw CliError("INVALID_ARGUMENT", "invalid wait condition");
    }
  }
}
std::string normalized_ref(std::string ref) {
  if (ref.size() >= 2 && ref.front() == '[' && ref.back() == ']')
    ref = ref.substr(1, ref.size() - 2);
  for (auto& c : ref)
    if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
  if (ref.size() < 2 || std::string("LBIS").find(ref[0]) == std::string::npos ||
      !std::all_of(ref.begin() + 1, ref.end(),
                   [](unsigned char c) { return c >= '0' && c <= '9'; }))
    throw CliError("INVALID_REF", "use a current L/B/I/S reference, for example I1");
  return ref;
}
}  // namespace webcli
