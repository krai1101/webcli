#include "network/data_script.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>

#include "core/errors.hpp"
#include "core/page.hpp"
#include "core/script_types.hpp"
namespace webcli {
bool is_data_script_url(const std::string& url) {
  return url.size() >= 5 && std::tolower(static_cast<unsigned char>(url[0])) == 'd' &&
         std::tolower(static_cast<unsigned char>(url[1])) == 'a' &&
         std::tolower(static_cast<unsigned char>(url[2])) == 't' &&
         std::tolower(static_cast<unsigned char>(url[3])) == 'a' && url[4] == ':';
}
std::string decode_data_script(const std::string& url) {
  auto fail = []() { throw CliError("SCRIPT_DATA_URL", "invalid JavaScript data URL"); };
  auto comma = url.find(',');
  if (!is_data_script_url(url) || comma == std::string::npos || comma > 512) fail();
  if (url.size() > kMaxScriptBytes * 3 + 1024)
    throw CliError("RESPONSE_TOO_LARGE", "encoded script data URL exceeds the limit");
  auto metadata = url.substr(5, comma - 5);
  std::transform(metadata.begin(), metadata.end(), metadata.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  const auto mime = metadata.substr(0, metadata.find(';'));
  if (mime.empty() || script_kind(mime) != ScriptKind::Classic) fail();
  const bool base64 =
      metadata.size() >= 7 && metadata.compare(metadata.size() - 7, 7, ";base64") == 0;
  const auto end = url.find('#', comma + 1);
  std::string decoded;
  auto hex = [](char c) {
    return c >= '0' && c <= '9'   ? c - '0'
           : c >= 'a' && c <= 'f' ? c - 'a' + 10
           : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                  : -1;
  };
  for (std::size_t i = comma + 1; i < (end == std::string::npos ? url.size() : end); ++i) {
    char c = url[i];
    if (c == '%' && i + 2 < (end == std::string::npos ? url.size() : end) && hex(url[i + 1]) >= 0 &&
        hex(url[i + 2]) >= 0) {
      c = static_cast<char>((hex(url[i + 1]) << 4) | hex(url[i + 2]));
      i += 2;
    }
    decoded += c;
  }
  if (base64) {
    decoded.erase(std::remove_if(decoded.begin(), decoded.end(),
                                 [](unsigned char c) {
                                   return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
                                          c == '\f';
                                 }),
                  decoded.end());
    if (decoded.size() % 4 == 0) {
      if (!decoded.empty() && decoded.back() == '=') decoded.pop_back();
      if (!decoded.empty() && decoded.back() == '=') decoded.pop_back();
    }
    if (decoded.size() % 4 == 1) fail();
    const std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    unsigned buffer = 0, bits = 0;
    for (char c : decoded) {
      auto digit = alphabet.find(c);
      if (digit == std::string_view::npos) fail();
      buffer = (buffer << 6) | static_cast<unsigned>(digit);
      bits += 6;
      if (bits >= 8) {
        bits -= 8;
        output += static_cast<char>((buffer >> bits) & 255);
      }
    }
    decoded = std::move(output);
  }
  if (decoded.size() > kMaxScriptBytes)
    throw CliError("RESPONSE_TOO_LARGE", "decoded script data URL exceeds the limit");
  return decoded;
}
}  // namespace webcli
