#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

#include "network/http_client.hpp"

namespace webcli {
// Shared transport executor. Jobs contain native data only; promise delivery
// remains on the owning document's scheduler. Futures never join pool threads.
class ResourceService {
 public:
  ResourceService();
  ~ResourceService();
  std::future<HttpResponse> submit(HttpRequest);
  std::future<HttpResponse> submit(std::function<HttpResponse()>);
  static bool on_transport_thread();

 private:
  void run();
  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<std::packaged_task<HttpResponse()>> queue_;
  std::vector<std::thread> workers_;
  bool stopping_ = false;
};
ResourceService& resource_service();
void shutdown_resource_service();
// Transport implementation; callers use perform_http or the resource service.
HttpResponse perform_transfer(const HttpRequest&);
}  // namespace webcli
