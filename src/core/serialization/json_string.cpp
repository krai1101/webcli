#include "core/serialization/json_string.hpp"

namespace webcli {
std::string json_escape(const std::string& value) {
  constexpr char hex[] = "0123456789abcdef";
  std::string result = "\"";
  for (unsigned char c : value) switch (c) {
      case '"':
        result += "\\\"";
        break;
      case '\\':
        result += "\\\\";
        break;
      case '\n':
        result += "\\n";
        break;
      case '\r':
        result += "\\r";
        break;
      case '\t':
        result += "\\t";
        break;
      default:
        if (c < 0x20) {
          result += "\\u00";
          result += hex[c >> 4];
          result += hex[c & 15];
        } else
          result += static_cast<char>(c);
    }
  return result + '"';
}
}  // namespace webcli
