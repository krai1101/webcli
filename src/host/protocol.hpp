#pragma once
#include <cstdint>

#include "cli/commands.hpp"
namespace webcli {
inline constexpr std::size_t kMaxFrame = 4 * 1024 * 1024;
inline constexpr std::uint32_t kRequestProtocolVersion = 7;
struct Response {
  int status = 0;
  std::string output, error;
  bool rejected_before_execution = false;
};
std::string encode_request(const Request&);
Request decode_request(const std::string&);
std::string encode_response(const Response&);
Response decode_response(const std::string&);
std::string frame(const std::string&);
bool extract_frame(std::string& buffer, std::string& payload);
}  // namespace webcli
