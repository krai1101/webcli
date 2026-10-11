#pragma once
#include <ostream>

#include "cli/commands.hpp"
#include "core/page.hpp"
namespace webcli {
void render(const Page&, const Request&, std::ostream&);
void render_error(std::ostream&, bool json, const std::string& code, const std::string& message,
                  bool retryable = false);
}  // namespace webcli
