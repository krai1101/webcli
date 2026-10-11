#pragma once

#include <sqlite3.h>

#include <memory>
#include <string>

namespace webcli {
// A document opens its origin's backing file lazily. Destruction rolls back an
// outstanding transaction, including when navigation discards the document.
class IndexedDbStore {
 public:
  explicit IndexedDbStore(const std::string& origin);
  ~IndexedDbStore();
  IndexedDbStore(const IndexedDbStore&) = delete;
  IndexedDbStore& operator=(const IndexedDbStore&) = delete;
  using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;
  Statement prepare(const char* sql);
  void check(int status) const;

 private:
  sqlite3* database_ = nullptr;
};
}  // namespace webcli
