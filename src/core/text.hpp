#pragma once
#include <cstddef>
#include <string>
namespace webcli {
// Collapse whitespace for readable text; truncate without splitting UTF-8.
std::string trim(const std::string&);
std::string truncate(const std::string&, std::size_t);
}  // namespace webcli
