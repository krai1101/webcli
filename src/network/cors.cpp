#include "network/cors.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>

#include "core/errors.hpp"

namespace webcli {
namespace {
std::string lower(std::string value) {
  for (auto& c : value)
    if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
  return value;
}
std::set<std::string> tokens(const std::string& value, bool lowercase = true) {
  std::set<std::string> result;
  std::istringstream input(value);
  for (std::string item; std::getline(input, item, ',');) {
    const auto first = item.find_first_not_of(" \t"), last = item.find_last_not_of(" \t");
    if (first != std::string::npos) {
      item = item.substr(first, last - first + 1);
      result.insert(lowercase ? lower(item) : item);
    }
  }
  return result;
}
std::string header(const HttpResponse& response, const char* name) {
  const auto found = response.headers.find(name);
  return found == response.headers.end() ? "" : found->second;
}
bool simple_method(const std::string& method) {
  return method == "GET" || method == "HEAD" || method == "POST";
}
bool safe_header(const std::string& name, const std::string& value) {
  if (value.size() > 128) return false;
  if (name == "accept-language" || name == "content-language")
    return std::all_of(value.begin(), value.end(), [](unsigned char c) {
      return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
             std::string(" *,-.;=").find(c) != std::string::npos;
    });
  if (name != "accept" && name != "content-type") return false;
  if (std::any_of(value.begin(), value.end(), [](unsigned char c) {
        return (c < 32 && c != 9) || c == 127 ||
               std::string("\"():<>?@[\\]{}").find(c) != std::string::npos;
      }))
    return false;
  if (name == "accept") return true;
  const auto type = lower(value.substr(0, value.find(';')));
  return type == "application/x-www-form-urlencoded" || type == "multipart/form-data" ||
         type == "text/plain";
}
bool forbidden(const std::string& name) {
  static const std::set<std::string> names{"accept-charset",
                                           "accept-encoding",
                                           "access-control-request-headers",
                                           "access-control-request-method",
                                           "connection",
                                           "content-length",
                                           "cookie",
                                           "cookie2",
                                           "date",
                                           "dnt",
                                           "expect",
                                           "host",
                                           "keep-alive",
                                           "origin",
                                           "permissions-policy",
                                           "referer",
                                           "te",
                                           "trailer",
                                           "transfer-encoding",
                                           "upgrade",
                                           "via",
                                           "x-http-method",
                                           "x-http-method-override",
                                           "x-method-override"};
  return names.count(name) || name.rfind("sec-", 0) == 0 || name.rfind("proxy-", 0) == 0;
}
}  // namespace

void prepare_fetch(HttpRequest& request) {
  std::map<std::string, std::string> normalized;
  for (const auto& pair : request.headers) {
    auto name = lower(pair.first);
    if (name.empty() ||
        !std::all_of(name.begin(), name.end(),
                     [](unsigned char c) {
                       return std::isalnum(c) ||
                              std::string("!#$%&'*+-.^_`|~").find(c) != std::string::npos;
                     }) ||
        std::any_of(pair.second.begin(), pair.second.end(),
                    [](unsigned char c) { return (c < 32 && c != 9) || c == 127; }))
      throw CliError("INVALID_HEADER", "invalid fetch header");
    if (forbidden(name)) continue;
    auto& value = normalized[name];
    if (!value.empty()) value += ", ";
    value += pair.second;
  }
  request.headers = std::move(normalized);
  if (request.fetch_mode == "no-cors") {
    if (!simple_method(request.method))
      throw CliError("CORS_BLOCKED", "no-cors requires GET, HEAD or POST");
    for (auto it = request.headers.begin(); it != request.headers.end();)
      if (!safe_header(it->first, it->second))
        it = request.headers.erase(it);
      else
        ++it;
  }
}

std::optional<HttpRequest> cors_preflight(const HttpRequest& request, const std::string& target,
                                          const std::string& method,
                                          const std::map<std::string, std::string>& headers) {
  if (request.fetch_origin.empty() || request.fetch_mode != "cors" ||
      same_origin(request.fetch_origin, target))
    return std::nullopt;
  std::string unsafe;
  std::size_t safe_bytes = 0;
  for (const auto& pair : headers)
    if (safe_header(lower(pair.first), pair.second)) safe_bytes += pair.second.size();
  for (const auto& pair : headers) {
    const auto name = lower(pair.first);
    if (name == "origin") continue;
    if (!safe_header(name, pair.second) || safe_bytes > 1024) {
      if (!unsafe.empty()) unsafe += ", ";
      unsafe += name;
    }
  }
  if (simple_method(method) && unsafe.empty()) return std::nullopt;
  HttpRequest result = request;
  result.url = target;
  result.method = "OPTIONS";
  result.body.clear();
  result.headers = {{"Origin", request.fetch_origin}, {"Access-Control-Request-Method", method}};
  if (!unsafe.empty()) result.headers["Access-Control-Request-Headers"] = unsafe;
  result.fetch_origin.clear();
  result.fetch_mode.clear();
  result.use_cookies = false;
  result.is_form_submission = false;
  result.allow_retries = false;
  result.redirect_policy = "error";
  return result;
}
void check_cors(const HttpResponse& response, const HttpRequest& request) {
  const auto origin = header(response, "access-control-allow-origin");
  if (origin != request.fetch_origin && !(origin == "*" && !request.fetch_credentials))
    throw CliError("CORS_BLOCKED", "response does not allow the document origin");
  if (request.fetch_credentials && header(response, "access-control-allow-credentials") != "true")
    throw CliError("CORS_BLOCKED", "response does not allow credentials");
}
void check_preflight(const HttpResponse& response, const HttpRequest& request,
                     const HttpRequest& preflight) {
  if (response.status < 200 || response.status >= 300)
    throw CliError("CORS_BLOCKED", "preflight failed");
  check_cors(response, request);
  const auto methods = tokens(header(response, "access-control-allow-methods"), false);
  const auto& method = preflight.headers.at("Access-Control-Request-Method");
  if (!simple_method(method) && !methods.count(method) &&
      !(methods.count("*") && !request.fetch_credentials))
    throw CliError("CORS_BLOCKED", "preflight rejected the method");
  const auto allow = tokens(header(response, "access-control-allow-headers"));
  const auto requested = preflight.headers.find("Access-Control-Request-Headers");
  if (requested != preflight.headers.end())
    for (const auto& name : tokens(requested->second))
      if (!allow.count(name) &&
          !(name != "authorization" && allow.count("*") && !request.fetch_credentials))
        throw CliError("CORS_BLOCKED", "preflight rejected a request header");
}
void filter_fetch_response(HttpResponse& response, const HttpRequest& request,
                           bool crossed_origin) {
  if (request.fetch_origin.empty()) return;
  if (request.fetch_mode == "no-cors" && crossed_origin) {
    response.status = 0;
    response.url.clear();
    response.body.clear();
    response.headers.clear();
    response.content_type.clear();
    response.opaque = true;
    return;
  }
  if (!crossed_origin) return;
  auto exposed = tokens(header(response, "access-control-expose-headers"));
  static const std::set<std::string> safe{"cache-control", "content-language", "content-length",
                                          "content-type",  "expires",          "last-modified",
                                          "pragma"};
  for (auto it = response.headers.begin(); it != response.headers.end();)
    if (!safe.count(it->first) && !exposed.count(it->first) &&
        !(exposed.count("*") && !request.fetch_credentials))
      it = response.headers.erase(it);
    else
      ++it;
}
}  // namespace webcli
