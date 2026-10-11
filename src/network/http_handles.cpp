#include "network/http_handles.hpp"

#include <array>
#include <mutex>
#include <vector>

#include "core/errors.hpp"

namespace webcli {
namespace {
using Handle = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;
struct Pool {
  std::mutex mutex;
  std::array<std::vector<Handle>, 2> idle;
};
Pool& pool() {
  static Pool value;
  return value;
}
}  // namespace
void initialize_http_transport() {
  // vcpkg's HTTP/2 feature may enable multiple TLS backends on Windows.
  // Select the common backend before libcurl starts any TLS state.
  if (curl_global_sslset(CURLSSLBACKEND_OPENSSL, nullptr, nullptr) != CURLSSLSET_OK)
    throw CliError("HTTP_INIT", "libcurl must provide the OpenSSL TLS backend");
  if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
    throw CliError("HTTP_INIT", "cannot initialize libcurl");
}
HttpHandle::HttpHandle(bool cookies) : cookies_(cookies) {
  auto& cache = pool();
  {
    std::lock_guard<std::mutex> lock(cache.mutex);
    auto& bucket = cache.idle[cookies_ ? 1 : 0];
    if (!bucket.empty()) {
      handle_ = std::move(bucket.back());
      bucket.pop_back();
    }
  }
  if (!handle_) handle_.reset(curl_easy_init());
  if (!handle_) throw CliError("HTTP_CLIENT_ERROR", "could not initialize libcurl");
  // Reset options/pointers, retaining connection, DNS and TLS caches. Cookie
  // sharing also survives reset, hence the separate immutable credential pools.
  curl_easy_reset(handle_.get());
  curl_easy_setopt(handle_.get(), CURLOPT_MAXCONNECTS, 8L);
}
HttpHandle::~HttpHandle() {
  if (!handle_) return;
  curl_easy_reset(handle_.get());
  try {
    auto& cache = pool();
    std::lock_guard<std::mutex> lock(cache.mutex);
    auto& bucket = cache.idle[cookies_ ? 1 : 0];
    if (bucket.size() < 16) bucket.push_back(std::move(handle_));
  } catch (...) {
    // Destruction must not throw if retaining a handle fails.
  }
}
void shutdown_http_handles() noexcept {
  auto& cache = pool();
  std::lock_guard<std::mutex> lock(cache.mutex);
  for (auto& bucket : cache.idle) bucket.clear();
}
}  // namespace webcli
