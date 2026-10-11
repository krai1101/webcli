#pragma once

#include <string>
#include <vector>

namespace webcli {
class ExecutionBudget;
// Parse static module requests without evaluating source or doing network I/O.
std::vector<std::string> module_requests(const std::string& source, const std::string& origin,
                                         ExecutionBudget& budget);
}  // namespace webcli
