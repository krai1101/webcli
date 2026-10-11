#pragma once

#include <map>
#include <string>
#include <vector>

namespace webcli {
// Response embedding policies are checked against every ancestor before the
// response body is parsed or its scripts execute.
bool frame_response_allowed(const std::map<std::string, std::string>& headers,
                            const std::string& origin, const std::vector<std::string>& ancestors);
}  // namespace webcli
