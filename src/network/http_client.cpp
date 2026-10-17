#include "network/http_client.hpp"
#include "network/fetch_metadata.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>

#include "core/browser_identity.hpp"
#include "core/errors.hpp"
#include "core/page.hpp"
#include "core/url/url.hpp"
#include "network/cookie_store.hpp"
#include "network/cors.hpp"
#include "network/data_script.hpp"
#include "network/destination_policy.hpp"
#include "network/http_handles.hpp"
#include "network/referrer.hpp"

namespace webcli {
namespace {
bool transient_transfer_error(CURLcode code) {
  switch (code) {
    case CURLE_COULDNT_CONNECT:
    case CURLE_SSL_CONNECT_ERROR:
    case CURLE_SEND_ERROR:
    case CURLE_RECV_ERROR:
    case CURLE_GOT_NOTHING:
    case CURLE_PARTIAL_FILE:
    case CURLE_HTTP2:
    case CURLE_HTTP2_STREAM:
      return true;
    default:
      return false;
  }
}
void wait_for_retry(const HttpRequest& request, std::chrono::steady_clock::time_point deadline,
                    int retry) {
  const auto until =
      std::min(deadline, std::chrono::steady_clock::now() + std::chrono::milliseconds(200 * retry));
  while (std::chrono::steady_clock::now() < until) {
    if (request.is_cancelled()) throw CliError("ABORTED", "HTTP request aborted");
    std::this_thread::sleep_until(
        std::min(until, std::chrono::steady_clock::now() + std::chrono::milliseconds(25)));
  }
}
std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}
struct Transfer {
  HttpResponse response;
  const HttpRequest* request;
  bool too_large = false, headers_too_large = false;
  std::size_t header_bytes = 0;
};
size_t receive(char* data, size_t size, size_t count, void* opaque) noexcept {
  auto& t = *static_cast<Transfer*>(opaque);
  if (size && count > std::numeric_limits<size_t>::max() / size) return 0;
  const size_t bytes = size * count;
  if (bytes > t.request->max_body_bytes - t.response.body.size()) {
    t.too_large = true;
    return 0;
  }
  try {
    t.response.body.append(data, bytes);
  } catch (...) {
    return 0;
  }
  return bytes;
}
size_t header(char* data, size_t size, size_t count, void* opaque) noexcept {
  if (size && count > std::numeric_limits<size_t>::max() / size) return 0;
  const size_t bytes = size * count;
  auto& t = *static_cast<Transfer*>(opaque);
  if (bytes > 1024 * 1024 - t.header_bytes) {
    t.too_large = true;
    t.headers_too_large = true;
    return 0;
  }
  t.header_bytes += bytes;
  try {
    std::string line(data, bytes);
    if (line.rfind("HTTP/", 0) == 0) t.response.headers.clear();
    auto pos = line.find(':');
    if (pos != std::string::npos) {
      auto value = line.substr(pos + 1);
      const auto begin = value.find_first_not_of(" \t");
      const auto end = value.find_last_not_of(" \t\r\n");
      const auto name = lower(line.substr(0, pos));
      const auto text = begin == std::string::npos ? "" : value.substr(begin, end - begin + 1);
      auto found = t.response.headers.find(name);
      if (found != t.response.headers.end() && name != "set-cookie")
        found->second += ", " + text;
      else
        t.response.headers[name] = text;
    }
  } catch (...) {
    return 0;
  }
  return bytes;
}
int progress(void* opaque, curl_off_t, curl_off_t, curl_off_t, curl_off_t) noexcept {
  auto* t = static_cast<Transfer*>(opaque);
  return t->request->is_cancelled() ? 1 : 0;
}
}  // namespace

static HttpResponse perform_http_transfer(const HttpRequest& request, bool& request_started) {
  const auto deadline = request.deadline_start + std::chrono::milliseconds(request.timeout_ms);
  if (request.is_cancelled()) throw CliError("ABORTED", "HTTP request aborted");
  std::string target = normalize_url(request.url);
  std::string method = request.method;
  std::string body = request.body;
  auto request_headers = request.headers;
  // Match the locale exposed by Navigator, while preserving explicit headers.
  if (std::none_of(request_headers.begin(), request_headers.end(),
                   [](const auto& item) { return lower(item.first) == "accept-language"; }))
    request_headers.emplace("Accept-Language", "en-US,en;q=0.9");
  // Engine-owned hints identify this runtime, including for subresources.
  // Erase case variants so a supplied header cannot produce conflicting brands.
  for (auto it = request_headers.begin(); it != request_headers.end();) {
    const auto name = lower(it->first);
    if (name == "sec-ch-ua" || name == "sec-ch-ua-full-version-list" || name.rfind("sec-fetch-", 0) == 0)
      it = request_headers.erase(it);
    else
      ++it;
  }
  auto referrer_source = request.referrer_source;
  auto referrer_policy = request.referrer_policy;
  int site_relationship = -2;
  auto cookies = profile_cookies();
  // Eligibility belongs to the original request: a POST redirected to GET must
  // not accidentally gain permission to repeat a submission workflow.
  const bool retry_allowed = request.allow_retries && !request.is_form_submission &&
                             (request.method == "GET" || request.method == "HEAD");
  int retries = 0;
  bool fresh_connection = false;
  bool crossed_origin = false;
  for (int redirect = 0; redirect <= 10;) {
    if (request.is_cancelled()) throw CliError("ABORTED", "HTTP request aborted");
    if (!request.allowed_origin.empty() && !same_origin(request.allowed_origin, target))
      throw CliError("CROSS_ORIGIN_UNSUPPORTED", "cross-origin fetch or redirect is unsupported");
    const bool cross = !request.fetch_origin.empty() && !same_origin(request.fetch_origin, target);
    crossed_origin = crossed_origin || cross;
    if (auto preflight = cors_preflight(request, target, method, request_headers)) {
      bool dispatched = false;
      check_preflight(perform_http_transfer(*preflight, dispatched), request, *preflight);
    }
    if (!request.fetch_origin.empty() && (cross || (method != "GET" && method != "HEAD")))
      request_headers["origin"] = request.fetch_origin;
    // Native navigation POSTs carry the document origin without becoming CORS
    // fetches. A redirect rewritten to GET must drop our synthesized Origin.
    if (request.fetch_origin.empty() && request.metadata_mode == "navigate" &&
        !request.metadata_origin.empty()) {
      for (auto it = request_headers.begin(); it != request_headers.end();)
        if (lower(it->first) == "origin") it = request_headers.erase(it);
        else ++it;
      if (method != "GET" && method != "HEAD")
        request_headers["Origin"] = request.metadata_origin;
    }
    const bool use_cookies =
        request.use_cookies && cookies &&
        (request.cookie_origin.empty() || same_origin(request.cookie_origin, target));
    HttpHandle curl(use_cookies);
    Transfer transfer{{}, &request};
    char error[CURL_ERROR_SIZE]{};
    auto* c = curl.get();
    DestinationPolicy destination;
    destination.attach(c);
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                               deadline - std::chrono::steady_clock::now())
                               .count();
    if (remaining <= 0) throw CliError("NETWORK_TIMEOUT", "HTTP request timed out", true);
    curl_easy_setopt(c, CURLOPT_URL, target.c_str());
    curl_easy_setopt(c, CURLOPT_FRESH_CONNECT, fresh_connection ? 1L : 0L);
    fresh_connection = false;
    curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "http,https");
    // Do not rely on version-dependent defaults, particularly when this curl
    // build lacks HTTP/2. Explicit 2TLS still negotiates HTTP/1.1 when needed.
    const auto* capabilities = curl_version_info(CURLVERSION_NOW);
    const long http_version = capabilities && (capabilities->features & CURL_VERSION_HTTP2)
                                  ? static_cast<long>(CURL_HTTP_VERSION_2TLS)
                                  : static_cast<long>(CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(c, CURLOPT_HTTP_VERSION, http_version);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS,
                     std::min<long>(kHttpConnectTimeoutMs, static_cast<long>(remaining)));
    curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, static_cast<long>(remaining));
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(c, CURLOPT_USERAGENT, kUserAgent.c_str());
#ifdef _WIN32
    // OpenSSL must use Windows' trust store rather than requiring a private
    // CA bundle. This keeps peer and hostname verification enabled.
    if (curl_easy_setopt(c, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NATIVE_CA) != CURLE_OK)
      throw CliError("HTTP_INIT", "cannot enable native certificate trust");
#endif
    const auto referrer = request_referrer(referrer_source, target, referrer_policy);
    curl_easy_setopt(c, CURLOPT_REFERER, referrer.empty() ? nullptr : referrer.c_str());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, receive);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &transfer);
    curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, header);
    curl_easy_setopt(c, CURLOPT_HEADERDATA, &transfer);
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, error);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, progress);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, &transfer);
    if (use_cookies) cookies->attach(c);
    if (method == "HEAD")
      curl_easy_setopt(c, CURLOPT_NOBODY, 1L);
    else if (method != "GET") {
      curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, method.c_str());
      curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.data());
      curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size()));
    }
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(nullptr,
                                                                        curl_slist_free_all);
    // POSTFIELDS otherwise makes libcurl invent a form content type, even for
    // binary Fetch/Beacon bodies whose BodyInit has no inferred media type.
    if (method != "GET" && method != "HEAD" &&
        std::none_of(request_headers.begin(), request_headers.end(),
                     [](const auto& item) { return lower(item.first) == "content-type"; }))
      request_headers.emplace("Content-Type", "");
    const auto append_header = [&](const std::string& name, const std::string& value) {
      if (name.find_first_of("\r\n:") != std::string::npos ||
          value.find_first_of("\r\n") != std::string::npos)
        throw CliError("INVALID_HEADER", "HTTP headers contain invalid characters");
      auto* next = curl_slist_append(headers.get(), (name + ": " + value).c_str());
      if (!next) throw std::bad_alloc();
      headers.release();
      headers.reset(next);
    };
    for (const auto& pair : request_headers) append_header(pair.first, pair.second);
    // Add transport-owned hints after CORS checks: they are not author headers
    // and must not cause a preflight, including after redirects.
    append_header("Sec-Ch-Ua", kClientHintBrand);
    append_header("Sec-Ch-Ua-Full-Version-List", kClientHintFullVersion);
    for (const auto& pair : fetch_metadata_headers(request, target, site_relationship))
      append_header(pair.first, pair.second);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers.get());
    const auto code = curl_easy_perform(c);
    // Request size is conservative evidence that dispatch may have happened,
    // not evidence that the remote application committed the operation.
    long request_bytes = 0;
    curl_easy_getinfo(c, CURLINFO_REQUEST_SIZE, &request_bytes);
    request_started = request_started || request_bytes > 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &transfer.response.status);
    char* type = nullptr;
    curl_easy_getinfo(c, CURLINFO_CONTENT_TYPE, &type);
    if (type) transfer.response.content_type = type;
    transfer.response.url = target;
    transfer.response.request_referrer = referrer;
    transfer.response.request_referrer_policy = referrer_policy;
    if (code != CURLE_OK) {
      if (destination.blocked())
        throw CliError("PRIVATE_NETWORK_BLOCKED",
                       "private-network connection blocked by configuration");
      if (transfer.too_large)
        throw CliError("RESPONSE_TOO_LARGE", transfer.headers_too_large
                                                 ? "response headers exceed the 1048576-byte limit"
                                                 : "response body exceeds the " +
                                                       std::to_string(request.max_body_bytes) +
                                                       "-byte limit");
      if (code == CURLE_ABORTED_BY_CALLBACK) throw CliError("ABORTED", "HTTP request aborted");
      const bool transient = transient_transfer_error(code);
      if (retry_allowed && transient && transfer.response.status < 400 && retries < 2 &&
          std::chrono::steady_clock::now() < deadline) {
        // The next attempt gets its own response buffer and a new connection.
        // Redirects, backoff and attempts all consume the original deadline.
        wait_for_retry(request, deadline, ++retries);
        fresh_connection = true;
        continue;
      }
      const std::string detail = std::string(error[0] ? error : curl_easy_strerror(code)) +
                                 " (curl code " + std::to_string(static_cast<int>(code)) +
                                 "; automatic retries: " + std::to_string(retries) + ")";
      throw CliError("NETWORK_ERROR", detail,
                     retry_allowed && transfer.response.status < 400 &&
                         (transient || code == CURLE_OPERATION_TIMEDOUT));
    }
    // Flush received cookies before the next redirect, script, or fetch starts.
    if (cookies) cookies->persist();
    if (cross && request.fetch_mode == "cors") check_cors(transfer.response, request);
    const auto status = transfer.response.status;
    if ((status == 301 || status == 302 || status == 303 || status == 307 || status == 308) &&
        transfer.response.headers.count("location")) {
      if (request.redirect_policy == "manual") {
        if (!request.fetch_origin.empty()) {
          transfer.response = HttpResponse{};
          transfer.response.opaque = true;
          transfer.response.content_type = "opaqueredirect";
        }
        return std::move(transfer.response);
      }
      if (request.redirect_policy == "error")
        throw CliError("REDIRECT_REJECTED", "request policy rejects redirects");
      if (redirect == 10) throw CliError("REDIRECT_LIMIT", "too many HTTP redirects");
      // Redirects may tighten policy; previously removed path information must
      // not reappear if a later hop chooses a more permissive policy.
      referrer_source = referrer;
      if (const auto policy = transfer.response.headers.find("referrer-policy");
          policy != transfer.response.headers.end())
        referrer_policy = response_referrer_policy(policy->second, referrer_policy);
      auto resolved = resolve_web_url(target, transfer.response.headers.at("location"));
      if (!resolved) throw CliError("INVALID_REDIRECT", "invalid redirect URL");
      if (!request.fetch_origin.empty() && crossed_origin && !same_origin(target, *resolved))
        throw CliError("CORS_REDIRECT_UNSUPPORTED", "cross-origin redirect chain is unsupported");
      if (!same_origin(target, *resolved)) {
        for (auto entry = request_headers.begin(); entry != request_headers.end();) {
          const auto name = lower(entry->first);
          if (name == "authorization" || name == "proxy-authorization" || name == "cookie")
            entry = request_headers.erase(entry);
          else
            ++entry;
        }
      }
      target = *resolved;
      if ((status == 303 && method != "HEAD") ||
          ((status == 301 || status == 302) && method == "POST")) {
        method = "GET";
        body.clear();
        for (auto entry = request_headers.begin(); entry != request_headers.end();) {
          const auto name = lower(entry->first);
          if (name == "content-type" || name == "content-length" || name == "content-encoding")
            entry = request_headers.erase(entry);
          else
            ++entry;
        }
      }
      ++redirect;
      continue;
    }
    filter_fetch_response(transfer.response, request, crossed_origin);
    return std::move(transfer.response);
  }
  throw CliError("REDIRECT_LIMIT", "too many HTTP redirects");
}

HttpResponse perform_transfer(const HttpRequest& request) {
  bool request_started = false;
  try {
    return perform_http_transfer(request, request_started);
  } catch (const CliError& error) {
    const bool submission =
        request.is_form_submission || (request.method != "GET" && request.method != "HEAD");
    if (submission && request_started)
      throw CliError("SUBMISSION_OUTCOME_UNKNOWN",
                     "The request may have been processed, but its response could not be "
                     "completed. Check the site's confirmation or account records before "
                     "submitting again. Cause [" +
                         error.code + "]: " + error.what(),
                     false);
    throw;
  }
}

std::optional<std::string> resolve_web_url(const std::string& base, const std::string& relative) {
  ParsedUrl url;
  if (!url.parse(relative, base)) return std::nullopt;
  const auto scheme = url.protocol();
  if (scheme != "http" && scheme != "https") return std::nullopt;
  return url.href();
}
std::string normalize_url(std::string input) {
  const auto prefix = lower(input.substr(0, 6));
  if (input.find("://") == std::string::npos && prefix.compare(0, 5, "http:") != 0 &&
      prefix != "https:")
    input = "https://" + input;
  ParsedUrl url;
  if (!url.parse(input) || (url.protocol() != "http" && url.protocol() != "https"))
    throw CliError("INVALID_URL", "provide a valid HTTP or HTTPS URL");
  return url.href();
}
std::string url_origin(const std::string& input) {
  ParsedUrl url;
  if (!url.parse(input)) return {};
  auto origin = url.origin();
  return origin == "null" ? "" : origin;
}
bool same_origin(const std::string& a, const std::string& b) {
  const auto origin = url_origin(a);
  return !origin.empty() && origin == url_origin(b);
}
std::string encode_component(const std::string& value) {
  if (value.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
    throw CliError("VALUE_TOO_LARGE", "form value too large");
  char* raw = curl_easy_escape(nullptr, value.c_str(), static_cast<int>(value.size()));
  if (!raw) throw std::bad_alloc();
  std::string result(raw);
  curl_free(raw);
  return result;
}
std::string encode_form_component(const std::string& value) {
  constexpr char hex[] = "0123456789ABCDEF";
  std::string result;
  for (unsigned char byte : value) {
    if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
        (byte >= '0' && byte <= '9') || byte == '*' || byte == '-' || byte == '.' || byte == '_')
      result += static_cast<char>(byte);
    else if (byte == ' ')
      result += '+';
    else {
      result += '%';
      result += hex[byte >> 4];
      result += hex[byte & 15];
    }
  }
  return result;
}
std::string form_get_url(const std::string& action, const std::string& query) {
  ParsedUrl url;
  if (!url.parse(action) || (url.protocol() != "http" && url.protocol() != "https") ||
      !url.assign("search", query.empty() ? "?" : "?" + query))
    throw CliError("INVALID_URL", "invalid form action URL");
  return url.href();
}
std::string fetch_script(const std::string& url, long timeout_ms,
                         const std::string& cookie_origin) {
  if (is_data_script_url(url)) return decode_data_script(url);
  HttpRequest request;
  request.url = url;
  request.metadata_mode = "no-cors";
  request.metadata_origin = cookie_origin;
  request.destination = "script";
  request.timeout_ms = timeout_ms;
  request.max_body_bytes = kMaxScriptBytes;
  request.cookie_origin = cookie_origin;
  try {
    auto response = perform_http(request);
    if (response.status < 200 || response.status >= 300)
      throw CliError("SCRIPT_HTTP_STATUS",
                     "script returned HTTP " + std::to_string(response.status),
                     response.status >= 500 || response.status == 429);
    return std::move(response.body);
  } catch (const CliError& error) {
    throw CliError(error.code, std::string(error.what()) + ": " + url, error.retryable);
  }
}
}  // namespace webcli
