#pragma once
#include <curl/curl.h>

namespace webcli {
// One instance per transfer; the callback lives through curl_easy_perform.
class DestinationPolicy {
 public:
  void attach(CURL*);
  bool blocked() const { return blocked_; }

 private:
  bool blocked_ = false;
  static curl_socket_t open_socket(void*, curlsocktype, curl_sockaddr*) noexcept;
};
}  // namespace webcli
