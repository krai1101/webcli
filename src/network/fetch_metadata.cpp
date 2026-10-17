#include "network/fetch_metadata.hpp"

#include <libpsl.h>

#include <algorithm>

#include "core/url/url.hpp"
#include "network/http_client.hpp"
namespace webcli {
namespace {
bool same_site(ParsedUrl& source, ParsedUrl& target) {
  if (source.protocol() != target.protocol()) return false;
  const auto one = source.hostname(), two = target.hostname();
  if (one == two) return true;
  const auto ip = [](const std::string& host) {
    return host.find(':') != std::string::npos ||
           host.find_first_not_of("0123456789.") == std::string::npos;
  };
  if (ip(one) || ip(two)) return false;
  const auto* first = psl_registrable_domain(psl_builtin(), one.c_str());
  const auto* second = psl_registrable_domain(psl_builtin(), two.c_str());
  return first && second && std::string(first) == second;
}
bool trustworthy(const ParsedUrl& target) {
  if (target.protocol() == "https") return true;
  const auto host = target.hostname();
  return target.protocol() == "http" &&
         (host == "localhost" || host == "[::1]" || host == "::1" ||
          (host.rfind("127.", 0) == 0 &&
           host.find_first_not_of("0123456789.") == std::string::npos) ||
          (host.size() > 10 && host.compare(host.size() - 10, 10, ".localhost") == 0));
}
}  // namespace
bool potentially_trustworthy_url(const std::string& url) {
  ParsedUrl target;
  return target.parse(url) && trustworthy(target);
}
std::map<std::string, std::string> fetch_metadata_headers(const HttpRequest& request,
                                                          const std::string& url,
                                                          int& relationship) {
  const auto mode = request.fetch_mode.empty() ? request.metadata_mode : request.fetch_mode;
  if (mode.empty()) return {};
  const auto origin = !request.fetch_origin.empty()      ? request.fetch_origin
                      : !request.metadata_origin.empty() ? request.metadata_origin
                                                         : request.referrer_source;
  ParsedUrl target, source;
  if (!target.parse(url)) return {};
  if (relationship != -1) {
    if (mode == "navigate" && origin.empty())
      relationship = -1;
    else {
      int site = 2;
      if (source.parse(origin)) {
        if (source.origin() == target.origin())
          site = 0;
        else if (same_site(source, target))
          site = 1;
      }
      relationship = std::max(relationship, site);
    }
  }
  if (!trustworthy(target)) return {};
  const char* site = relationship == -1  ? "none"
                     : relationship == 0 ? "same-origin"
                     : relationship == 1 ? "same-site"
                                         : "cross-site";
  std::map<std::string, std::string> headers{
      {"Sec-Fetch-Site", site}, {"Sec-Fetch-Mode", mode}, {"Sec-Fetch-Dest", request.destination}};
  if (mode == "navigate" && relationship == -1) headers.emplace("Sec-Fetch-User", "?1");
  return headers;
}
}  // namespace webcli
