#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <thread>

#include "host/protocol.hpp"
#include "storage/profile_store.hpp"

namespace webcli {
// One runtime owner per browsing instance. The resident IPC loop never enters
// QuickJS or waits for an instance's network, script, or command operation.
class InstanceWorker {
 public:
  explicit InstanceWorker(std::shared_ptr<OriginStorage>);
  ~InstanceWorker();
  InstanceWorker(const InstanceWorker&) = delete;
  InstanceWorker& operator=(const InstanceWorker&) = delete;
  std::future<Response> enqueue(Request);
  void stop();
  bool finished() const { return finished_.load(); }

 private:
  struct Work {
    Request request;
    std::promise<Response> reply;
  };
  void run(std::shared_ptr<OriginStorage>);
  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<Work> queue_;
  bool stopping_ = false;
  std::atomic<bool> finished_{false};
  std::shared_ptr<std::atomic<bool>> cancelled_ = std::make_shared<std::atomic<bool>>(false);
  std::thread thread_;
};
}  // namespace webcli
