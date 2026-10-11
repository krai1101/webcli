#pragma once
#include <quickjs.h>

#include <map>
#include <optional>
#include <string>

namespace webcli {
// Bounded inline import maps. Registration closes at the first resolution so
// already linked modules cannot silently change identity.
class ImportMap {
 public:
  void add(JSContext*, const std::string& source, const std::string& base);
  std::string resolve(const std::string& specifier, const std::string& referrer);

 private:
  using Specifiers = std::map<std::string, std::optional<std::string>>;
  Specifiers imports_;
  std::map<std::string, Specifiers> scopes_;
  std::size_t entries_ = 0;
  bool resolved_ = false;
  Specifiers parse_specifiers(JSContext*, JSValueConst, const std::string&, std::size_t&);
  static std::optional<std::string> match(const Specifiers&, const std::string&);
};
}  // namespace webcli
