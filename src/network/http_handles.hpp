#pragma once
#include <curl/curl.h>

#include <memory>

namespace webcli {
// Once on the main thread, before any handles or transfer workers exist.
void initialize_http_transport();
// Each lease is exclusive. Cookie-enabled and anonymous handles never mix.
class HttpHandle {
 public:
  explicit HttpHandle(bool cookies);
  ~HttpHandle();
  HttpHandle(const HttpHandle&) = delete;
  HttpHandle& operator=(const HttpHandle&) = delete;
  CURL* get() const { return handle_.get(); }

 private:
  bool cookies_;
  std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> handle_{nullptr, curl_easy_cleanup};
};
// After transfers finish, before destroying the cookie share/global curl state.
void shutdown_http_handles() noexcept;
}  // namespace webcli
