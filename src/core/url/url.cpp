#include "core/url/url.hpp"

#include <new>

#include "core/errors.hpp"
#include "core/url/url_components.hpp"

namespace webcli {
namespace {
struct Output {
  std::string text;
  bool failed = false;
};
lxb_status_t append(const lxb_char_t* data, size_t size, void* opaque) noexcept {
  if (!size) return LXB_STATUS_OK;
  auto& output = *static_cast<Output*>(opaque);
  try {
    output.text.append(reinterpret_cast<const char*>(data), size);
    return LXB_STATUS_OK;
  } catch (...) {
    output.failed = true;
    return LXB_STATUS_ERROR_MEMORY_ALLOCATION;
  }
}
template <typename Function, typename Value>
std::string serialize(Function fn, Value value) {
  Output output;
  const auto status = fn(value, append, &output);
  if (output.failed) throw std::bad_alloc();
  if (status != LXB_STATUS_OK) throw CliError("URL_SERIALIZATION", "cannot serialize URL");
  return std::move(output.text);
}
std::string serialize_href(const lxb_url_t* url) {
  return serialize([](const lxb_url_t* value, lexbor_serialize_cb_f cb,
                      void* ctx) { return lxb_url_serialize(value, cb, ctx, false); },
                   url);
}
std::string serialize_origin(const lxb_url_t* url, lxb_url_parser_t* parser) {
  const auto scheme = serialize(lxb_url_serialize_scheme, url);
  if (scheme == "blob") {
    lxb_url_parser_clean(parser);
    const auto* inner = lxb_url_parse(parser, nullptr, url->path.str.data, url->path.str.length);
    if (!inner) return "null";
    const auto inner_scheme = serialize(lxb_url_serialize_scheme, inner);
    if (inner_scheme != "http" && inner_scheme != "https") return "null";
    return serialize_origin(inner, parser);
  }
  if (scheme != "http" && scheme != "https" && scheme != "ws" && scheme != "wss" && scheme != "ftp")
    return "null";
  auto result = scheme + "://" + serialize(lxb_url_serialize_host, &url->host);
  if (url->has_port) result += ":" + std::to_string(url->port);
  return result;
}
lxb_status_t set(lxb_url_t* url, lxb_url_parser_t* parser, const std::string& name,
                 const std::string& text) {
  const auto* data = reinterpret_cast<const lxb_char_t*>(text.data());
  const auto size = text.size();
  if ((name == "username" || name == "password" || name == "port") &&
      (url->scheme.type == LXB_URL_SCHEMEL_TYPE_FILE ||
       serialize(lxb_url_serialize_host, &url->host).empty()))
    return LXB_STATUS_OK;
  if (name == "username") return lxb_url_api_username_set(url, data, size);
  if (name == "password") return lxb_url_api_password_set(url, data, size);
  if (name == "host") return set_url_host(url, parser, text);
  using Setter = lxb_status_t (*)(lxb_url_t*, lxb_url_parser_t*, const lxb_char_t*, size_t);
  const std::pair<const char*, Setter> setters[] = {
      {"href", lxb_url_api_href_set},         {"protocol", lxb_url_api_protocol_set},
      {"hostname", lxb_url_api_hostname_set}, {"port", lxb_url_api_port_set},
      {"pathname", lxb_url_api_pathname_set}, {"search", lxb_url_api_search_set},
      {"hash", lxb_url_api_hash_set}};
  for (const auto& setter : setters)
    if (name == setter.first) return setter.second(url, parser, data, size);
  throw CliError("URL_PROPERTY", "unknown URL component");
}
}  // namespace
void ParsedUrl::ParserDelete::operator()(lxb_url_parser_t* parser) const {
  if (parser->mraw) lxb_url_parser_memory_destroy(parser);
  lxb_url_parser_destroy(parser, true);
}
ParsedUrl::ParsedUrl() : parser_(lxb_url_parser_create()) {
  if (!parser_ || lxb_url_parser_init(parser_.get(), nullptr) != LXB_STATUS_OK)
    throw std::bad_alloc();
}
bool ParsedUrl::parse(std::string input, const std::optional<std::string>& base) {
  if (input.size() > kMaxBytes || (base && base->size() > kMaxBytes))
    throw CliError("URL_TOO_LARGE", "URL or base exceeds 1 MiB");
  url_ = nullptr;
  lxb_url_parser_clean(parser_.get());
  lexbor_mraw_clean(parser_->mraw);
  const lxb_url_t* parsed_base = nullptr;
  if (base) {
    const auto text = prepare_url_input(*base);
    parsed_base = lxb_url_parse(parser_.get(), nullptr,
                                reinterpret_cast<const lxb_char_t*>(text.data()), text.size());
    if (!parsed_base) return false;
    lxb_url_parser_clean(parser_.get());
  }
  input = prepare_url_input(std::move(input));
  url_ = lxb_url_parse(parser_.get(), parsed_base,
                       reinterpret_cast<const lxb_char_t*>(input.data()), input.size());
  if (!url_) return false;
  normalize_url_components(url_);
  return true;
}
bool ParsedUrl::assign(const std::string& name, std::string value) {
  if (!url_) throw CliError("INVALID_URL", "URL is not parsed");
  if (value.size() > kMaxBytes) throw CliError("URL_TOO_LARGE", "URL component exceeds 1 MiB");
  if (name == "href") value = prepare_url_input(std::move(value));
  auto* candidate = lxb_url_clone(url_->mraw, url_);
  if (!candidate) throw std::bad_alloc();
  lxb_url_parser_clean(parser_.get());
  const auto status = set(candidate, parser_.get(), name, value);
  if (status == LXB_STATUS_ERROR_MEMORY_ALLOCATION) throw std::bad_alloc();
  if (status != LXB_STATUS_OK && name == "href") return false;
  // Host parsing can commit a valid hostname before rejecting a port.
  if (status == LXB_STATUS_OK || name == "host") url_ = candidate;
  normalize_url_components(url_);
  return true;
}
const lxb_url_t* ParsedUrl::require_value() const {
  if (!url_) throw CliError("INVALID_URL", "URL is not parsed");
  return url_;
}
std::string ParsedUrl::href() const { return serialize_href(require_value()); }
std::string ParsedUrl::protocol() const {
  return serialize(lxb_url_serialize_scheme, require_value());
}
std::string ParsedUrl::hostname() const {
  return serialize(lxb_url_serialize_host, &require_value()->host);
}
std::string ParsedUrl::pathname() const {
  return serialize(lxb_url_serialize_path, &require_value()->path);
}
std::string ParsedUrl::origin() { return serialize_origin(require_value(), parser_.get()); }
ParsedUrl::Parts ParsedUrl::parts() {
  const auto* url = require_value();
  auto* parser = parser_.get();
  const auto scheme = serialize(lxb_url_serialize_scheme, url);
  const auto hostname = serialize(lxb_url_serialize_host, &url->host);
  const auto port = url->has_port ? std::to_string(url->port) : "";
  return {{{"href", serialize_href(url)},
           {"protocol", scheme + ":"},
           {"username", serialize(lxb_url_serialize_username, url)},
           {"password", serialize(lxb_url_serialize_password, url)},
           {"hostname", hostname},
           {"port", port},
           {"host", hostname + (port.empty() ? "" : ":" + port)},
           {"pathname", serialize(lxb_url_serialize_path, &url->path)},
           {"search", url->query.length ? "?" + serialize(lxb_url_serialize_query, url) : ""},
           {"hash", url->fragment.length
                        ? "#" + std::string(reinterpret_cast<const char*>(url->fragment.data),
                                            url->fragment.length)
                        : ""},
           {"origin", serialize_origin(url, parser)}}};
}
}  // namespace webcli
