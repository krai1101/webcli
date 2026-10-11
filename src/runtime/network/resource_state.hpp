#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "runtime/network/resource_job.hpp"
namespace webcli {
// Realm-owned request capabilities and buffered blob data. No transport worker
// accesses this state or its JavaScript handles.
struct ResourceState {
  int next_http_id = 1;
  unsigned int requests = 0, image_requests = 0;
  std::unordered_map<std::string, std::string> blob_urls;
  std::size_t blob_url_bytes = 0;
  std::vector<std::unique_ptr<FetchJob>> fetches;
};
}  // namespace webcli
