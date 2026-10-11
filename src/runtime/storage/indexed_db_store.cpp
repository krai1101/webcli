#include "runtime/storage/indexed_db_store.hpp"

#include <openssl/sha.h>

#include <array>
#include <filesystem>
#include <stdexcept>

#include "storage/profile_store.hpp"

namespace webcli {
IndexedDbStore::IndexedDbStore(const std::string& origin) {
  std::array<unsigned char, SHA256_DIGEST_LENGTH> digest{};
  SHA256(reinterpret_cast<const unsigned char*>(origin.data()), origin.size(), digest.data());
  std::string name;
  for (auto byte : digest) {
    name += "0123456789abcdef"[byte >> 4];
    name += "0123456789abcdef"[byte & 15];
  }
  const auto directory = profile_directory() / "indexeddb";
  std::filesystem::create_directories(directory);
  const auto path = (directory / (name + ".sqlite")).u8string();
  const int status =
      sqlite3_open_v2(path.c_str(), &database_,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX, nullptr);
  try {
    check(status);
    sqlite3_extended_result_codes(database_, 1);
    sqlite3_busy_timeout(database_, 1000);
    const char* schema =
        "PRAGMA journal_mode=WAL; PRAGMA foreign_keys=ON;"
        "CREATE TABLE IF NOT EXISTS databases(name TEXT PRIMARY KEY, version INTEGER NOT NULL);"
        "CREATE TABLE IF NOT EXISTS stores(database_name TEXT NOT NULL, name TEXT NOT NULL,"
        " key_path TEXT, automatic INTEGER NOT NULL, next_key INTEGER NOT NULL DEFAULT 1,"
        " PRIMARY KEY(database_name,name), FOREIGN KEY(database_name) REFERENCES databases(name)"
        " ON DELETE CASCADE);"
        "CREATE TABLE IF NOT EXISTS records(database_name TEXT NOT NULL, store_name TEXT NOT NULL,"
        " key BLOB NOT NULL, value BLOB NOT NULL, PRIMARY KEY(database_name,store_name,key),"
        " FOREIGN KEY(database_name,store_name) REFERENCES stores(database_name,name)"
        " ON DELETE CASCADE) WITHOUT ROWID;"
        "CREATE TABLE IF NOT EXISTS indexes(database_name TEXT NOT NULL, store_name TEXT NOT NULL,"
        " name TEXT NOT NULL, key_path TEXT NOT NULL, is_unique INTEGER NOT NULL,"
        " multi_entry INTEGER NOT NULL, PRIMARY KEY(database_name,store_name,name),"
        " FOREIGN KEY(database_name,store_name) REFERENCES stores(database_name,name)"
        " ON DELETE CASCADE) WITHOUT ROWID;"
        "CREATE TABLE IF NOT EXISTS index_records(database_name TEXT NOT NULL,"
        " store_name TEXT NOT NULL, index_name TEXT NOT NULL, key BLOB NOT NULL,"
        " primary_key BLOB NOT NULL, is_unique INTEGER NOT NULL,"
        " PRIMARY KEY(database_name,store_name,index_name,key,primary_key),"
        " FOREIGN KEY(database_name,store_name,index_name)"
        " REFERENCES indexes(database_name,store_name,name) ON DELETE CASCADE ON UPDATE CASCADE,"
        " FOREIGN KEY(database_name,store_name,primary_key)"
        " REFERENCES records(database_name,store_name,key) ON DELETE CASCADE) WITHOUT ROWID;"
        "CREATE UNIQUE INDEX IF NOT EXISTS unique_index_keys ON"
        " index_records(database_name,store_name,index_name,key) WHERE is_unique=1;";
    check(sqlite3_exec(database_, schema, nullptr, nullptr, nullptr));
  } catch (...) {
    sqlite3_close_v2(database_);
    database_ = nullptr;
    throw;
  }
}
IndexedDbStore::~IndexedDbStore() {
  if (!database_) return;
  if (!sqlite3_get_autocommit(database_))
    sqlite3_exec(database_, "ROLLBACK", nullptr, nullptr, nullptr);
  sqlite3_close_v2(database_);
}
void IndexedDbStore::check(int status) const {
  if (status == SQLITE_OK || status == SQLITE_ROW || status == SQLITE_DONE) return;
  const int primary = status & 255;
  const char* name = primary == SQLITE_CONSTRAINT                         ? "ConstraintError"
                     : primary == SQLITE_FULL                             ? "QuotaExceededError"
                     : primary == SQLITE_BUSY || primary == SQLITE_LOCKED ? "AbortError"
                                                                          : "UnknownError";
  throw std::runtime_error(std::string(name) + ": " + sqlite3_errmsg(database_));
}
IndexedDbStore::Statement IndexedDbStore::prepare(const char* sql) {
  sqlite3_stmt* raw = nullptr;
  const int status = sqlite3_prepare_v2(database_, sql, -1, &raw, nullptr);
  Statement result(raw, sqlite3_finalize);
  check(status);
  return result;
}
}  // namespace webcli
