#pragma once
#include <string>

namespace webcli {
// Quoted JSON string, also valid as a JavaScript string literal.
std::string json_escape(const std::string&);
}  // namespace webcli
