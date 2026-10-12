#include "core/runtime_policy.hpp"

#include <cstdlib>
#include <limits>
#include <memory>
#include <string>
#include <string_view>

#include "core/errors.hpp"

namespace webcli {
namespace {
std::string environment_value(const char* name) {
#ifdef _WIN32
  char* raw = nullptr;
  std::size_t length = 0;
  if (_dupenv_s(&raw, &length, name) != 0)
    throw CliError("INVALID_CONFIG", "cannot read runtime configuration");
  const std::unique_ptr<char, decltype(&std::free)> value(raw, std::free);
  return raw ? std::string(raw) : std::string{};
#else
  const auto* value = std::getenv(name);
  return value ? value : "";
#endif
}
RuntimePolicy read_policy() {
  RuntimePolicy policy;
  {
    const auto value = environment_value("WEBCLI_BLOCK_PRIVATE_NETWORKS");
    const std::string_view text(value);
    if (!text.empty() && text != "0" && text != "1")
      throw CliError("INVALID_CONFIG", "WEBCLI_BLOCK_PRIVATE_NETWORKS must be 0 or 1");
    policy.block_private_networks = text == "1";
  }
  {
    const auto value = environment_value("WEBCLI_JS_MAX_HEAP_MB");
    constexpr std::size_t unit = 1024 * 1024;
    constexpr auto maximum = std::numeric_limits<std::size_t>::max() / unit;
    const std::string_view text(value);
    std::size_t megabytes = 0;
    for (const auto digit : text) {
      if (digit < '0' || digit > '9' || megabytes > (maximum - (digit - '0')) / 10)
        throw CliError("INVALID_CONFIG",
                       "WEBCLI_JS_MAX_HEAP_MB must be a nonnegative integer in range");
      megabytes = megabytes * 10 + (digit - '0');
    }
    policy.js_heap_bytes = megabytes * unit;
  }
  return policy;
}
}  // namespace

const RuntimePolicy& runtime_policy() {
  static const RuntimePolicy policy = read_policy();
  return policy;
}
std::string RuntimePolicy::identity() const {
  return std::string(block_private_networks ? "private-blocked:" : "private-allowed:") +
         std::to_string(js_heap_bytes);
}
}  // namespace webcli
