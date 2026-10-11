#pragma once

#include <lexbor/url/url.h>

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace webcli {
// An operation-local URL arena. Never shares mutable parser state across
// threads.
class ParsedUrl {
 public:
  static constexpr std::size_t kMaxBytes = 1024 * 1024;
  using Parts = std::array<std::pair<const char*, std::string>, 11>;
  ParsedUrl();
  bool parse(std::string input, const std::optional<std::string>& base = std::nullopt);
  bool assign(const std::string& name, std::string value);
  std::string href() const;
  std::string protocol() const;
  std::string hostname() const;
  std::string pathname() const;
  std::string origin();
  Parts parts();

 private:
  struct ParserDelete {
    void operator()(lxb_url_parser_t*) const;
  };
  std::unique_ptr<lxb_url_parser_t, ParserDelete> parser_;
  lxb_url_t* url_ = nullptr;
  const lxb_url_t* require_value() const;
};
}  // namespace webcli
