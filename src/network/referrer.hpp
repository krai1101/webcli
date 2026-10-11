#pragma once

#include <string>
#include <string_view>

namespace webcli {
struct NavigationReferrer {
  std::string source, policy;
};
bool valid_referrer_policy(std::string_view);
std::string response_referrer_policy(std::string_view, const std::string& current);
std::string request_referrer(const std::string& source, const std::string& target,
                             const std::string& policy);
}  // namespace webcli
