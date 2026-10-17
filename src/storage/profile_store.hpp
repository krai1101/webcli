#pragma once
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>

#include "core/page.hpp"

namespace webcli {
struct OriginStorage {
  std::recursive_mutex mutex;
  std::map<std::string, std::map<std::string, std::string>> values;
};
std::filesystem::path profile_directory();
std::filesystem::path diagnostic_directory();
void append_script_error_log(const Page&, std::size_t first = 0);
void append_runtime_trace_log(const Page&);
class StorageStore {
 public:
  explicit StorageStore(std::shared_ptr<OriginStorage> local = std::make_shared<OriginStorage>())
      : local_(std::move(local)) {}
  std::map<std::string, std::string>& data(const std::string& origin, bool persistent);
  void save(const std::string& origin);
  std::unique_lock<std::recursive_mutex> lock() {
    return std::unique_lock<std::recursive_mutex>(local_->mutex);
  }

 private:
  std::shared_ptr<OriginStorage> local_;
  OriginStorage transient_;
};
}  // namespace webcli
