#pragma once
#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include "core/page.hpp"

namespace webcli {
struct HttpRequest {
  std::string url, method = "GET", body, allowed_origin;
  std::string fetch_origin, fetch_mode;
  bool fetch_credentials = false;
  std::string cookie_origin;  // Empty means the caller permits cookies on every origin.
  std::string referrer_source, referrer_policy;
  bool use_cookies = true;
  std::size_t max_body_bytes = kMaxBodyBytes;
  std::map<std::string, std::string> headers;
  long timeout_ms = kHttpRequestTimeoutMs;
  // Transfer timeout starts at dispatch; queue wait belongs to the enclosing
  // document/operation deadline, not to DNS/connect/response time.
  std::chrono::steady_clock::time_point deadline_start = std::chrono::steady_clock::now();
  std::shared_ptr<std::atomic<bool>> cancelled, owner_cancelled;
  bool is_cancelled() const {
    return (cancelled && cancelled->load()) || (owner_cancelled && owner_cancelled->load());
  }
  // Native form submissions disable this even when their method is GET.
  bool allow_retries = true;
  bool is_form_submission = false;
  std::string redirect_policy = "follow";
};
struct HttpResponse {
  long status = 0;
  std::string url, body, content_type;
  std::map<std::string, std::string> headers;
  // Actual final-hop request metadata, after redirect reductions.
  std::string request_referrer, request_referrer_policy;
  bool opaque = false;
};
HttpResponse perform_http(const HttpRequest&);
std::string fetch_script(const std::string&, long timeout_ms = kScriptRequestTimeoutMs,
                         const std::string& cookie_origin = "");
std::optional<std::string> resolve_web_url(const std::string&, const std::string&);
bool same_origin(const std::string&, const std::string&);
std::string normalize_url(std::string);
std::string url_origin(const std::string&);
std::string encode_component(const std::string&);
std::string encode_form_component(const std::string&);
std::string form_get_url(const std::string& action, const std::string& query);
}  // namespace webcli
