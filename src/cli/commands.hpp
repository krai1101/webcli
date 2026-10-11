#pragma once
#include <cstddef>
#include <ostream>
#include <string>
#include <vector>

#include "runtime/page/engine_request.hpp"
namespace webcli {
struct Request {
  std::string command, document, instance;
  std::vector<std::string> arguments;
  bool json = false, debug = false, offset_set = false;
  std::size_t offset = 0, limit = 80;
  unsigned timeout_ms = 5000;
};
struct CommandSpec {
  const char* name;
  const char* syntax;
  const char* description;
  std::size_t min_args, max_args;
};
const std::vector<CommandSpec>& command_specs();
Request parse_request(const std::vector<std::string>&);
void validate_request(const Request&);
std::vector<std::string> split_prompt_line(const std::string&);
EngineRequest engine_request(const Request&);
void show_help(std::ostream&);
int run_prompt(const Request& startup);
}  // namespace webcli
