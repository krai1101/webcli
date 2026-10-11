#include "runtime/frames/frame_policy.hpp"

#include <algorithm>
#include <optional>
#include <sstream>

#include "core/text.hpp"
#include "core/url/url.hpp"

namespace webcli {
namespace {
std::string lower(std::string value) {
  for (auto& character : value)
    if (character >= 'A' && character <= 'Z') character += 'a' - 'A';
  return value;
}
bool source_matches(const std::string& source, const std::string& self,
                    const std::string& ancestor) {
  if (ancestor == "null") return false;
  if (source == "*") return true;
  if (source == "'self'") return self == ancestor;
  if (source.empty() || source.front() == '\'') return false;
  if (source.back() == ':' && source.find('/') == std::string::npos)
    return ancestor.compare(0, source.size(), source) == 0;
  // Exact origins are normalized by the same URL parser as navigation. The
  // common wildcard subdomain source excludes the bare registrable hostname.
  auto pattern = source;
  const auto separator = pattern.find("://");
  if (separator == std::string::npos) pattern = self.substr(0, self.find("://") + 3) + pattern;
  const auto wildcard = pattern.find("://*.");
  if (wildcard != std::string::npos) {
    auto normalized = pattern;
    normalized.erase(wildcard + 3, 2);
    ParsedUrl expected, actual;
    if (!expected.parse(normalized) || !actual.parse(ancestor)) return false;
    const auto host = actual.hostname(), suffix = "." + expected.hostname();
    if (host.size() <= suffix.size() ||
        host.compare(host.size() - suffix.size(), suffix.size(), suffix))
      return false;
    // Replacing the actual hostname retains its scheme/port for comparison.
    if (!actual.assign("hostname", expected.hostname())) return false;
    return actual.origin() == expected.origin();
  }
  ParsedUrl expected;
  return expected.parse(pattern) && expected.origin() == ancestor;
}
}  // namespace
bool frame_response_allowed(const std::map<std::string, std::string>& headers,
                            const std::string& origin, const std::vector<std::string>& ancestors) {
  bool has_ancestors = false;
  if (const auto csp = headers.find("content-security-policy"); csp != headers.end()) {
    std::istringstream policies(csp->second);
    for (std::string policy; std::getline(policies, policy, ',');) {
      std::istringstream directives(policy);
      for (std::string directive; std::getline(directives, directive, ';');) {
        std::istringstream parts(directive);
        std::string name;
        parts >> name;
        if (lower(name) != "frame-ancestors") continue;
        has_ancestors = true;
        std::vector<std::string> sources;
        for (std::string source; parts >> source;) sources.push_back(lower(source));
        for (const auto& ancestor : ancestors)
          if (std::none_of(sources.begin(), sources.end(), [&](const auto& source) {
                return source_matches(source, origin, ancestor);
              }))
            return false;
        break;  // Only the first occurrence of a directive applies per policy.
      }
    }
  }
  if (has_ancestors) return true;  // CSP frame-ancestors takes precedence over XFO.
  const auto xfo = headers.find("x-frame-options");
  if (xfo == headers.end()) return true;
  std::istringstream values(xfo->second);
  std::optional<std::string> first;
  bool conflict = false, known = false;
  for (std::string value; std::getline(values, value, ',');) {
    value = lower(trim(value));
    known = known || value == "deny" || value == "sameorigin" || value == "allowall";
    if (first)
      conflict = conflict || *first != value;
    else
      first = value;
  }
  if (!first) return true;
  if ((known && conflict) || *first == "deny") return false;
  if (*first != "sameorigin") return true;
  return std::all_of(ancestors.begin(), ancestors.end(),
                     [&](const auto& value) { return value == origin; });
}
}  // namespace webcli
