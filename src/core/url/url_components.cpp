#include "core/url/url_components.hpp"

#include <algorithm>
#include <new>

namespace webcli {
namespace {
std::string str(const lexbor_str_t& value) {
  return value.length ? std::string(reinterpret_cast<const char*>(value.data), value.length) : "";
}
void replace(lexbor_str_t& target, lexbor_mraw_t* memory, const std::string& value) {
  lexbor_str_t result{};
  if (!lexbor_str_init(&result, memory, value.size()) ||
      !lexbor_str_append(&result, memory, reinterpret_cast<const lxb_char_t*>(value.data()),
                         value.size()))
    throw std::bad_alloc();
  lexbor_str_destroy(&target, memory, false);
  target = result;
}
}  // namespace
std::string prepare_url_input(std::string value) {
  const auto control = [](unsigned char byte) { return byte <= 0x20; };
  const auto begin = std::find_if_not(value.begin(), value.end(), control);
  const auto end = std::find_if_not(value.rbegin(), value.rend(), control).base();
  if (begin >= end) return "";
  const auto leading = static_cast<std::size_t>(begin - value.begin());
  value.resize(static_cast<std::size_t>(end - value.begin()));
  value.erase(0, leading);
  value.erase(
      std::remove_if(value.begin(), value.end(),
                     [](char byte) { return byte == '\t' || byte == '\n' || byte == '\r'; }),
      value.end());
  const auto colon = value.find(':');
  const auto first_separator = value.find_first_of("/\\?#");
  const bool absolute = colon != std::string::npos && colon < first_separator;
  auto scheme = absolute ? value.substr(0, colon) : "";
  std::transform(scheme.begin(), scheme.end(), scheme.begin(), [](char byte) {
    return byte >= 'A' && byte <= 'Z' ? static_cast<char>(byte + 32) : byte;
  });
  const bool special = scheme == "http" || scheme == "https" || scheme == "ws" || scheme == "wss" ||
                       scheme == "ftp" || scheme == "file";
  std::size_t start = absolute ? colon + 1 : 0;
  if (absolute && !special && (start == value.size() || value[start] != '/')) return value;
  const auto separators = special ? "/\\" : "/";
  if (special && scheme != "file") {
    start = value.find_first_not_of(separators, start);
    start = value.find_first_of("/\\?#", start);
  } else if (value.compare(start, 2, "//") == 0) {
    start = value.find_first_of(special ? "/\\?#" : "/?#", start + 2);
  }
  if (start == std::string::npos) return value;
  const auto path_end = std::min(value.size(), value.find_first_of("?#", start));
  if (path_end == value.size() || path_end == start) return value;
  const auto last_separator = value.find_last_of(separators, path_end - 1);
  const auto segment =
      last_separator == std::string::npos || last_separator < start ? start : last_separator + 1;
  unsigned dots = 0;
  auto cursor = segment;
  while (cursor < path_end && dots < 3) {
    if (value[cursor] == '.')
      ++cursor;
    else if (cursor + 2 < path_end && value[cursor] == '%' && value[cursor + 1] == '2' &&
             (value[cursor + 2] == 'e' || value[cursor + 2] == 'E')) {
      cursor += 3;
    } else
      break;
    ++dots;
  }
  // A terminal dot segment and a terminal dot segment followed by '/' are
  // equivalent. Lexbor otherwise advances past the query/fragment delimiter.
  if (cursor == path_end && dots > 0 && dots <= 2) value.insert(path_end, 1, '/');
  return value;
}
void normalize_url_components(lxb_url_t* url) {
  // Current encode sets include '^' in hierarchical paths. Opaque paths
  // preserve internal spaces but encode their final space before ?/#.
  auto path = str(url->path.str);
  bool changed = false;
  if (url->path.opaque) {
    if (!path.empty() && path.back() == ' ' && (url->query.data || url->fragment.data)) {
      path.replace(path.size() - 1, 1, "%20");
      changed = true;
    }
  } else if (path.find('^') != std::string::npos) {
    std::string encoded;
    for (char value : path) {
      if (value == '^')
        encoded += "%5E";
      else
        encoded += value;
    }
    path = std::move(encoded);
    changed = true;
  }
  if (changed) replace(url->path.str, url->mraw, path);
  if (url->host.type == LXB_URL_HOST_TYPE_DOMAIN) {
    auto host = str(url->host.u.domain);
    std::transform(host.begin(), host.end(), host.begin(), [](unsigned char value) {
      return value >= 'A' && value <= 'Z' ? static_cast<char>(value + 32)
                                          : static_cast<char>(value);
    });
    if (url->scheme.type == LXB_URL_SCHEMEL_TYPE_FILE && host == "localhost")
      url->host.type = LXB_URL_HOST_TYPE_EMPTY;
    else if (host != str(url->host.u.domain))
      replace(url->host.u.domain, url->mraw, host);
  }
}
lxb_status_t set_url_host(lxb_url_t* url, lxb_url_parser_t* parser, const std::string& input) {
  if (url->path.opaque) return LXB_STATUS_OK;
  std::string value;
  value.reserve(input.size());
  for (char byte : input)
    if (byte != '\t' && byte != '\n' && byte != '\r') value += byte;
  const bool special = url->scheme.type != LXB_URL_SCHEMEL_TYPE__UNKNOWN &&
                       url->scheme.type != LXB_URL_SCHEMEL_TYPE__UNDEF;
  value.resize(std::min(value.size(), value.find_first_of(special ? "/?#\\" : "/?#")));
  if (value.empty() && (url->has_port || url->username.length || url->password.length))
    return LXB_STATUS_OK;
  if (url->scheme.type == LXB_URL_SCHEMEL_TYPE_FILE)
    return lxb_url_api_host_set(url, parser, reinterpret_cast<const lxb_char_t*>(value.data()),
                                value.size());
  const auto close = !value.empty() && value.front() == '[' ? value.find(']') : std::string::npos;
  const auto colon = value.find(':', close == std::string::npos ? 0 : close + 1);
  if (colon == 0) return LXB_STATUS_OK;
  const auto hostname = value.substr(0, colon);
  auto status = lxb_url_api_hostname_set(
      url, parser, reinterpret_cast<const lxb_char_t*>(hostname.data()), hostname.size());
  if (status != LXB_STATUS_OK || colon == std::string::npos || colon + 1 == value.size())
    return status;
  lxb_url_parser_clean(parser);
  const auto port = value.substr(colon + 1);
  return lxb_url_api_port_set(url, parser, reinterpret_cast<const lxb_char_t*>(port.data()),
                              port.size());
}
}  // namespace webcli
