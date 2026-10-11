#include "host/protocol.hpp"

#include <algorithm>

#include "core/errors.hpp"
namespace webcli {
namespace {
void integer(std::string& out, std::uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) out += static_cast<char>((value >> shift) & 255);
}
void string(std::string& out, const std::string& value) {
  if (value.size() > kMaxFrame) throw CliError("IPC_LIMIT", "IPC string too large");
  integer(out, static_cast<std::uint32_t>(value.size()));
  out += value;
}
struct Reader {
  const std::string& bytes;
  std::size_t offset = 0;
  std::uint32_t integer() {
    if (bytes.size() - offset < 4) throw CliError("IPC_PROTOCOL", "truncated IPC message");
    std::uint32_t value = 0;
    for (unsigned s = 0; s < 32; s += 8)
      value |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset++])) << s;
    return value;
  }
  std::string string() {
    auto length = integer();
    if (length > kMaxFrame || length > bytes.size() - offset)
      throw CliError("IPC_PROTOCOL", "invalid IPC string");
    auto value = bytes.substr(offset, length);
    offset += length;
    return value;
  }
  void end() {
    if (offset != bytes.size()) throw CliError("IPC_PROTOCOL", "trailing IPC data");
  }
};
}  // namespace
std::string encode_request(const Request& r) {
  std::string out;
  integer(out, kRequestProtocolVersion);
  string(out, r.command);
  string(out, r.instance);
  string(out, r.document);
  integer(out, r.json ? 1 : 0);
  integer(out, r.debug ? 1 : 0);
  integer(out, r.offset_set ? 1 : 0);
  integer(out, static_cast<std::uint32_t>(r.offset));
  integer(out, static_cast<std::uint32_t>(r.limit));
  integer(out, r.timeout_ms);
  integer(out, static_cast<std::uint32_t>(r.arguments.size()));
  for (const auto& value : r.arguments) string(out, value);
  return out;
}
Request decode_request(const std::string& bytes) {
  Reader reader{bytes};
  if (reader.integer() != kRequestProtocolVersion)
    throw CliError("IPC_VERSION", "worker protocol version mismatch; close old WebCLI instances");
  Request r;
  r.command = reader.string();
  r.instance = reader.string();
  if (r.instance.empty()) throw CliError("IPC_PROTOCOL", "browsing instance is required");
  r.document = reader.string();
  auto flag = reader.integer();
  if (flag > 1) throw CliError("IPC_PROTOCOL", "invalid JSON flag");
  r.json = flag == 1;
  flag = reader.integer();
  if (flag > 1) throw CliError("IPC_PROTOCOL", "invalid debug flag");
  r.debug = flag == 1;
  flag = reader.integer();
  if (flag > 1) throw CliError("IPC_PROTOCOL", "invalid offset flag");
  r.offset_set = flag == 1;
  r.offset = reader.integer();
  r.limit = reader.integer();
  r.timeout_ms = reader.integer();
  auto count = reader.integer();
  std::size_t maximum = 0;
  for (const auto& spec : command_specs()) maximum = std::max(maximum, spec.max_args);
  if (count > maximum) throw CliError("IPC_PROTOCOL", "too many command arguments");
  for (unsigned i = 0; i < count; ++i) r.arguments.push_back(reader.string());
  reader.end();
  validate_request(r);
  return r;
}
std::string encode_response(const Response& response) {
  std::string out;
  integer(out, static_cast<std::uint32_t>(response.status));
  string(out, response.output);
  string(out, response.error);
  integer(out, response.rejected_before_execution ? 1 : 0);
  return out;
}
Response decode_response(const std::string& bytes) {
  Reader reader{bytes};
  Response result;
  result.status = static_cast<int>(reader.integer());
  result.output = reader.string();
  result.error = reader.string();
  const auto rejected = reader.integer();
  if (rejected > 1) throw CliError("IPC_PROTOCOL", "invalid rejection flag");
  result.rejected_before_execution = rejected != 0;
  reader.end();
  return result;
}
std::string frame(const std::string& payload) {
  if (payload.size() > kMaxFrame) throw CliError("IPC_LIMIT", "IPC payload too large");
  std::string out;
  integer(out, static_cast<std::uint32_t>(payload.size()));
  out += payload;
  return out;
}
bool extract_frame(std::string& buffer, std::string& payload) {
  if (buffer.size() < 4) return false;
  Reader reader{buffer};
  const auto count = reader.integer();
  if (count > kMaxFrame) throw CliError("IPC_LIMIT", "IPC frame exceeds 4 MiB");
  if (buffer.size() < 4 + count) return false;
  if (buffer.size() != 4 + count)
    throw CliError("IPC_PROTOCOL", "one command per IPC connection is required");
  payload = buffer.substr(4, count);
  return true;
}
}  // namespace webcli
