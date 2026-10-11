#pragma once

#include <stdexcept>
#include <string>
#include <utility>

namespace webcli {

class CliError : public std::runtime_error {
 public:
  CliError(std::string code, std::string message, bool retryable = false)
      : std::runtime_error(std::move(message)), code(std::move(code)), retryable(retryable) {}
  std::string code;
  bool retryable;
};

}  // namespace webcli
