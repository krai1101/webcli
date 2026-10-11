#include "network/referrer.hpp"

#include <array>

#include "core/url/url.hpp"

namespace webcli {
bool valid_referrer_policy(std::string_view policy) {
  constexpr std::array<std::string_view, 8> policies{"no-referrer",
                                                     "no-referrer-when-downgrade",
                                                     "origin",
                                                     "origin-when-cross-origin",
                                                     "same-origin",
                                                     "strict-origin",
                                                     "strict-origin-when-cross-origin",
                                                     "unsafe-url"};
  for (const auto candidate : policies)
    if (candidate == policy) return true;
  return false;
}

std::string response_referrer_policy(std::string_view header, const std::string& current) {
  std::string result = current;
  std::size_t position = 0;
  while (position < header.size()) {
    const auto comma = header.find(',', position);
    auto token = header.substr(
        position, comma == std::string_view::npos ? header.size() - position : comma - position);
    while (!token.empty() && (token.front() == ' ' || token.front() == '\t'))
      token.remove_prefix(1);
    while (!token.empty() && (token.back() == ' ' || token.back() == '\t')) token.remove_suffix(1);
    std::string normalized(token);
    for (auto& c : normalized)
      if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    if (valid_referrer_policy(normalized)) result = normalized;
    if (comma == std::string_view::npos) break;
    position = comma + 1;
  }
  return result;
}

std::string request_referrer(const std::string& source, const std::string& target,
                             const std::string& policy) {
  if (source.empty() || policy == "no-referrer") return {};
  ParsedUrl from, to;
  if (!from.parse(source) || !to.parse(target)) return {};
  if (from.protocol() != "http" && from.protocol() != "https") return {};
  if (to.protocol() != "http" && to.protocol() != "https") return {};
  from.assign("username", "");
  from.assign("password", "");
  from.assign("hash", "");
  const auto origin = from.origin() + '/';
  auto full = from.href();
  if (full.size() > 4096) full = origin;
  const bool same = from.origin() == to.origin();
  const bool downgrade = from.protocol() == "https" && to.protocol() == "http";
  if (policy == "unsafe-url") return full;
  if (policy == "same-origin") return same ? full : std::string{};
  if (policy == "origin") return origin;
  if (policy == "origin-when-cross-origin") return same ? full : origin;
  if (policy == "no-referrer-when-downgrade") return downgrade ? std::string{} : full;
  if (policy == "strict-origin") return downgrade ? std::string{} : origin;
  // Empty or unknown policies use the Fetch default.
  if (same) return full;
  return downgrade ? std::string{} : origin;
}
}  // namespace webcli
