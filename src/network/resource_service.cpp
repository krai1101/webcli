#include "network/resource_service.hpp"

#include "core/errors.hpp"

namespace webcli {
namespace {
thread_local bool transport_thread = false;
}
ResourceService::ResourceService() {
  try {
    for (std::size_t i = 0; i < kResourceConcurrency; ++i) workers_.emplace_back([this] { run(); });
  } catch (...) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopping_ = true;
    }
    wake_.notify_all();
    for (auto& worker : workers_) worker.join();
    throw;
  }
}
ResourceService::~ResourceService() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
    queue_.clear();
  }
  wake_.notify_all();
  for (auto& worker : workers_) worker.join();
}
std::future<HttpResponse> ResourceService::submit(HttpRequest request) {
  return submit([request = std::move(request)]() mutable {
    request.deadline_start = std::chrono::steady_clock::now();
    if (request.is_cancelled())
      throw CliError("REQUEST_CANCELLED", "resource request was cancelled");
    return perform_transfer(request);
  });
}
std::future<HttpResponse> ResourceService::submit(std::function<HttpResponse()> operation) {
  std::packaged_task<HttpResponse()> task(std::move(operation));
  auto future = task.get_future();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_) throw CliError("RESOURCE_SERVICE_CLOSED", "resource service is shutting down");
    if (queue_.size() >= 4096)
      throw CliError("RESOURCE_QUEUE_FULL", "transport queue is full", true);
    queue_.push_back(std::move(task));
  }
  wake_.notify_one();
  return future;
}
void ResourceService::run() {
  transport_thread = true;
  while (true) {
    std::packaged_task<HttpResponse()> task;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      wake_.wait(lock, [&] { return stopping_ || !queue_.empty(); });
      if (queue_.empty()) return;
      task = std::move(queue_.front());
      queue_.pop_front();
    }
    task();
  }
}
bool ResourceService::on_transport_thread() { return transport_thread; }
namespace {
struct ServiceRegistry {
  std::mutex mutex;
  std::unique_ptr<ResourceService> service;
};
ServiceRegistry& registry() {
  static ServiceRegistry instance;
  return instance;
}
}  // namespace
ResourceService& resource_service() {
  auto& entry = registry();
  std::lock_guard<std::mutex> lock(entry.mutex);
  if (!entry.service) entry.service = std::make_unique<ResourceService>();
  return *entry.service;
}
void shutdown_resource_service() {
  std::unique_ptr<ResourceService> service;
  {
    auto& entry = registry();
    std::lock_guard<std::mutex> lock(entry.mutex);
    service = std::move(entry.service);
  }
  service.reset();
}
HttpResponse perform_http(const HttpRequest& request) {
  if (ResourceService::on_transport_thread()) {
    auto dispatched = request;
    dispatched.deadline_start = std::chrono::steady_clock::now();
    return perform_transfer(dispatched);
  }
  return resource_service().submit(request).get();
}
}  // namespace webcli
