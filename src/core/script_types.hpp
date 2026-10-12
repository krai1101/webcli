#pragma once
#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>

namespace webcli {
enum class ScriptKind { Data, Classic, Module };
inline bool script_uses_credentials(std::string_view value) {
  constexpr std::string_view expected = "use-credentials";
  if (value.size() != expected.size()) return false;
  for (std::size_t i = 0; i < value.size(); ++i) {
    const auto character = value[i] >= 'A' && value[i] <= 'Z' ? value[i] + ('a' - 'A') : value[i];
    if (character != expected[i]) return false;
  }
  return true;
}
inline std::string script_type_essence(std::string value) {
  const auto separator = value.find(';');
  if (separator != std::string::npos) value.resize(separator);
  const auto begin = value.find_first_not_of(" \t\r\n\f");
  if (begin == std::string::npos) return {};
  value = value.substr(begin, value.find_last_not_of(" \t\r\n\f") - begin + 1);
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}
inline ScriptKind script_kind(std::string value) {
  const auto type = script_type_essence(std::move(value));
  if (type.empty()) return ScriptKind::Classic;
  if (type == "module") return ScriptKind::Module;
  constexpr std::array<std::string_view, 16> javascript_types = {"application/ecmascript",
                                                                 "application/javascript",
                                                                 "application/x-ecmascript",
                                                                 "application/x-javascript",
                                                                 "text/ecmascript",
                                                                 "text/javascript",
                                                                 "text/javascript1.0",
                                                                 "text/javascript1.1",
                                                                 "text/javascript1.2",
                                                                 "text/javascript1.3",
                                                                 "text/javascript1.4",
                                                                 "text/javascript1.5",
                                                                 "text/jscript",
                                                                 "text/livescript",
                                                                 "text/x-ecmascript",
                                                                 "text/x-javascript"};
  return std::find(javascript_types.begin(), javascript_types.end(), type) != javascript_types.end()
             ? ScriptKind::Classic
             : ScriptKind::Data;
}
}  // namespace webcli
