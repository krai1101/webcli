#include "core/text.hpp"

#include <cctype>

namespace webcli {
std::string trim(const std::string& value) {
  std::string result;
  bool pending_space = false;
  for (std::size_t i = 0; i < value.size(); ++i) {
    const auto c = static_cast<unsigned char>(value[i]);
    std::size_t unicode_space = 0;
    if (i + 1 < value.size() && c == 0xC2 &&
        (static_cast<unsigned char>(value[i + 1]) == 0xA0 ||
         static_cast<unsigned char>(value[i + 1]) == 0x85))
      unicode_space = 2;
    if (i + 2 < value.size() && c == 0xE2) {
      const auto second = static_cast<unsigned char>(value[i + 1]);
      const auto third = static_cast<unsigned char>(value[i + 2]);
      if ((second == 0x80 &&
           ((third >= 0x80 && third <= 0x8A) || third == 0xA8 || third == 0xA9 || third == 0xAF)) ||
          (second == 0x81 && third == 0x9F))
        unicode_space = 3;
    }
    if (i + 2 < value.size() && c == 0xE3 && static_cast<unsigned char>(value[i + 1]) == 0x80 &&
        static_cast<unsigned char>(value[i + 2]) == 0x80)
      unicode_space = 3;
    if (std::isspace(c) || unicode_space) {
      pending_space = !result.empty();
      if (unicode_space) i += unicode_space - 1;
    } else {
      if (pending_space) result.push_back(' ');
      result.push_back(static_cast<char>(c));
      pending_space = false;
    }
  }
  return result;
}

std::string truncate(const std::string& value, std::size_t limit) {
  if (value.size() <= limit) return value;
  std::size_t end = limit;
  while (end > 0 && end < value.size() && (static_cast<unsigned char>(value[end]) & 0xC0) == 0x80)
    --end;
  return value.substr(0, end) + "...";
}
}  // namespace webcli
