#include <algorithm>
#include <array>
#include <cctype>
#include <ctime>
#include <optional>

#include "core/errors.hpp"
#include "core/url/url.hpp"
#include "network/cookie_store.hpp"
#include "network/public_suffix.hpp"

namespace webcli {
namespace {
std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}
std::string strip(std::string value) {
  const auto first = value.find_first_not_of(" \t");
  if (first == std::string::npos) return {};
  return value.substr(first, value.find_last_not_of(" \t") - first + 1);
}
struct Address {
  std::string host, path;
  bool secure = false;
};
Address address(const std::string& url) {
  ParsedUrl parsed;
  if (!parsed.parse(url) || (parsed.protocol() != "http" && parsed.protocol() != "https"))
    throw CliError("COOKIE_URL", "invalid cookie URL");
  return {parsed.hostname(), parsed.pathname(), parsed.protocol() == "https"};
}
bool domain_matches(const std::string& host, std::string domain, bool subdomains) {
  if (!domain.empty() && domain.front() == '.') domain.erase(0, 1);
  return host == domain || (subdomains && host.size() > domain.size() &&
                            host.compare(host.size() - domain.size(), domain.size(), domain) == 0 &&
                            host[host.size() - domain.size() - 1] == '.');
}
bool path_matches(const std::string& request, const std::string& cookie) {
  return request == cookie ||
         (request.size() > cookie.size() && request.compare(0, cookie.size(), cookie) == 0 &&
          (!cookie.empty() && (cookie.back() == '/' || request[cookie.size()] == '/')));
}
struct Cookie {
  std::array<std::string, 7> fields;
  bool http_only = false;
};
Cookie parse(std::string line) {
  Cookie cookie;
  if (line.rfind("#HttpOnly_", 0) == 0) {
    cookie.http_only = true;
    line.erase(0, 10);
  }
  std::size_t start = 0;
  for (std::size_t i = 0; i < cookie.fields.size(); ++i) {
    const auto end = i == 6 ? std::string::npos : line.find('\t', start);
    cookie.fields[i] = line.substr(start, end == std::string::npos ? end : end - start);
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return cookie;
}
bool alive(const Cookie& cookie) {
  if (cookie.fields[4] == "0") return true;
  try {
    return std::stoll(cookie.fields[4]) > static_cast<long long>(std::time(nullptr));
  } catch (...) {
    return false;
  }
}
}  // namespace
std::string CookieStore::document_cookie(const std::string& url) {
  const auto current = address(url);
  std::vector<Cookie> visible;
  for (const auto& line : snapshot()) {
    auto cookie = parse(line);
    if (!cookie.http_only && alive(cookie) && (cookie.fields[3] != "TRUE" || current.secure) &&
        domain_matches(current.host, cookie.fields[0], cookie.fields[1] == "TRUE") &&
        path_matches(current.path, cookie.fields[2]))
      visible.push_back(std::move(cookie));
  }
  std::stable_sort(visible.begin(), visible.end(), [](const Cookie& a, const Cookie& b) {
    return a.fields[2].size() > b.fields[2].size();
  });
  std::string result;
  for (const auto& cookie : visible) {
    if (!result.empty()) result += "; ";
    result += cookie.fields[5] + "=" + cookie.fields[6];
  }
  return result;
}
void write_document_cookie(CookieStore& store, const std::string& url, const std::string& input) {
  if (input.size() > 4096 ||
      std::any_of(input.begin(), input.end(), [](unsigned char c) { return c < 32 || c == 127; }))
    return;
  const auto current = address(url);
  const auto semicolon = input.find(';');
  const auto pair = strip(input.substr(0, semicolon));
  const auto equal = pair.find('=');
  if (equal == std::string::npos || equal == 0) return;
  const auto name = strip(pair.substr(0, equal)), value = strip(pair.substr(equal + 1));
  if (name.empty() || name.find_first_of(" ()<>@,:\\\"/[]?={}") != std::string::npos) return;
  std::string domain = current.host;
  const auto slash = current.path.rfind('/');
  std::string path = slash == 0 || slash == std::string::npos ? "/" : current.path.substr(0, slash);
  bool domain_attribute = false, domain_supplied = false, path_attribute = false, secure = false;
  long long expires = 0;
  std::optional<long long> max_age;
  std::size_t start = semicolon;
  while (start != std::string::npos) {
    const auto end = input.find(';', start + 1);
    auto entry = strip(input.substr(start + 1, end == std::string::npos ? end : end - start - 1));
    const auto eq = entry.find('=');
    const auto key = lower(strip(entry.substr(0, eq)));
    const auto val = eq == std::string::npos ? "" : strip(entry.substr(eq + 1));
    if (key == "domain") {
      domain = lower(val);
      if (!domain.empty() && domain.front() == '.') domain.erase(0, 1);
      domain_attribute = true;
      domain_supplied = true;
    } else if (key == "path" && !val.empty() && val.front() == '/') {
      path = val;
      path_attribute = true;
    } else if (key == "secure")
      secure = true;
    else if (key == "httponly")
      return;
    else if (key == "expires") {
      const auto time = curl_getdate(val.c_str(), nullptr);
      if (time >= 0) expires = static_cast<long long>(time);
    } else if (key == "max-age") {
      try {
        std::size_t used = 0;
        const auto age = std::stoll(val, &used);
        if (used == val.size()) max_age = std::clamp(age, -34560000LL, 34560000LL);
      } catch (...) {
      }
    }
    start = end;
  }
  // Cookie domains must use the same ASCII IDNA form as the native URL host.
  // Reject unnormalized Unicode rather than compare mismatched host forms.
  if (domain.empty() || (domain_supplied && domain.back() == '.') ||
      std::any_of(domain.begin(), domain.end(),
                  [](unsigned char c) { return c >= 128 || c <= 32 || c == 127; }) ||
      !domain_matches(current.host, domain, true))
    return;
  const bool ip_literal =
      current.host.find(':') != std::string::npos ||
      std::all_of(current.host.begin(), current.host.end(),
                  [](unsigned char c) { return (c >= '0' && c <= '9') || c == '.'; });
  if (ip_literal && domain != current.host) return;
  if (ip_literal || public_suffix(domain)) domain_attribute = false;
  if (domain != current.host && public_suffix(domain)) return;
  if (domain.find(':') != std::string::npos && domain != current.host) return;
  if (secure && !current.secure) return;
  const auto prefix = lower(name);
  if (prefix.rfind("__secure-", 0) == 0 && (!secure || !current.secure)) return;
  if (prefix.rfind("__host-", 0) == 0 &&
      (!secure || !current.secure || domain_supplied || !path_attribute || path != "/"))
    return;
  if (prefix.rfind("__http-", 0) == 0 || prefix.rfind("__host-http-", 0) == 0) return;
  if (max_age) expires = *max_age <= 0 ? 1 : static_cast<long long>(std::time(nullptr)) + *max_age;
  std::size_t count = 0;
  bool replacing = false;
  for (const auto& line : store.snapshot()) {
    const auto existing = parse(line);
    if (!alive(existing)) continue;
    ++count;
    auto existing_domain = existing.fields[0];
    if (!existing_domain.empty() && existing_domain.front() == '.') existing_domain.erase(0, 1);
    if (existing_domain == domain && existing.fields[2] == path && existing.fields[5] == name)
      replacing = true;
    if (existing.fields[5] == name &&
        ((existing.http_only && existing_domain == domain && existing.fields[2] == path) ||
         (!current.secure && existing.fields[3] == "TRUE" &&
          (domain_matches(domain, existing_domain, true) ||
           domain_matches(existing_domain, domain, true)) &&
          path_matches(path, existing.fields[2]))))
      return;
  }
  if (count >= 3000 && !replacing &&
      !(expires > 0 && expires <= static_cast<long long>(std::time(nullptr))))
    throw CliError("COOKIE_QUOTA", "profile cookie quota exceeded");
  const auto line = domain + "\t" + (domain_attribute ? "TRUE" : "FALSE") + "\t" + path + "\t" +
                    (secure ? "TRUE" : "FALSE") + "\t" + std::to_string(expires) + "\t" + name +
                    "\t" + value;
  store.import_cookie(line);
}
}  // namespace webcli
