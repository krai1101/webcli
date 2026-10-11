#include "network/public_suffix.hpp"

#include <curl/curl.h>

#include <iostream>
#include <memory>
#include <string>
#include <utility>

namespace {
// Exercise the shipped transport's HTTP Set-Cookie parser over loopback.
// Synthetic hosts are resolved locally, so this sends no external requests.
bool transport_cookies(const std::string& port) {
  std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> resolve(nullptr, curl_slist_free_all);
  const std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(),
                                                             curl_easy_cleanup);
  if (!curl) return false;
  curl_slist* addresses = nullptr;
  for (const auto* host : {"foo.co.uk", "foo.github.io"}) {
    const auto address = std::string(host) + ":" + port + ":127.0.0.1";
    auto* added = curl_slist_append(addresses, address.c_str());
    if (!added) {
      curl_slist_free_all(addresses);
      return false;
    }
    addresses = added;
  }
  resolve.reset(addresses);
  curl_easy_setopt(curl.get(), CURLOPT_RESOLVE, resolve.get());
  curl_easy_setopt(curl.get(), CURLOPT_PROXY, "");
  curl_easy_setopt(curl.get(), CURLOPT_COOKIEFILE, "");
  curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT_MS, 3000L);
  for (const auto& path :
       {"foo.co.uk/psl-public", "foo.github.io/psl-private", "foo.co.uk/psl-valid"}) {
    const auto slash = std::string(path).find('/');
    const auto url = "http://" + std::string(path).substr(0, slash) + ":" + port +
                     std::string(path).substr(slash);
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    if (curl_easy_perform(curl.get()) != CURLE_OK) return false;
  }
  curl_slist* raw = nullptr;
  if (curl_easy_getinfo(curl.get(), CURLINFO_COOKIELIST, &raw) != CURLE_OK) return false;
  const std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> cookies(raw,
                                                                         curl_slist_free_all);
  bool accepted = false;
  for (auto* row = raw; row; row = row->next) {
    const std::string value(row->data);
    if (value.find("\tpsl_unsafe\t") != std::string::npos) return false;
    if (value.find("\tpsl_safe\t") != std::string::npos) accepted = true;
  }
  return accepted;
}
}  // namespace

int main(int argc, char** argv) {
  if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) return 1;
  struct Cleanup {
    ~Cleanup() { curl_global_cleanup(); }
  } cleanup;
  const auto* version = curl_version_info(CURLVERSION_NOW);
  if (!version || !(version->features & CURL_VERSION_PSL)) {
    std::cerr << "Shipped libcurl has no PSL support\n";
    return 1;
  }
  const std::pair<const char*, bool> cases[] = {
      {"com", true}, {"co.uk", true}, {"example.co.uk", false},
      {"github.io", true}, {"user.github.io", false},
      {"ck", true}, {"foo.ck", true}, {"www.ck", false},
      {"kawasaki.jp", true}, {"foo.kawasaki.jp", true}, {"city.kawasaki.jp", false},
      {"invalid", true}, {"example.invalid", false},
      {"xn--p1ai", true}, {"example.xn--p1ai", false}};
  for (const auto& entry : cases) {
    if (webcli::public_suffix(entry.first) != entry.second) {
      std::cerr << "Incorrect public suffix classification: " << entry.first << '\n';
      return 1;
    }
  }
  if (argc > 1 && !transport_cookies(argv[1])) {
    std::cerr << "HTTP cookie public-suffix validation failed\n";
    return 1;
  }
  return 0;
}
