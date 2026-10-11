#pragma once
#include <curl/curl.h>

#include <memory>
#include <string>
#include <vector>

namespace webcli {
// All browsing instances share one cookie engine; transfer handles never write files.
class CookieStore {
 public:
  CookieStore();
  ~CookieStore();
  CookieStore(const CookieStore&) = delete;
  CookieStore& operator=(const CookieStore&) = delete;
  void attach(CURL*);
  void persist();
  std::string document_cookie(const std::string& url);
  void set_document_cookie(const std::string& url, const std::string& value);

 private:
  friend void write_document_cookie(CookieStore&, const std::string&, const std::string&);
  std::vector<std::string> snapshot();
  void import_cookie(const std::string& line);
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
std::shared_ptr<CookieStore> profile_cookies();
// Called before curl_global_cleanup, after all workers and requests have stopped.
void shutdown_cookie_store() noexcept;
}  // namespace webcli
