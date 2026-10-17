#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
namespace webcli {
struct WorkerMessage {
  std::vector<uint8_t> bytes;
  std::string error;
};
// Queues contain serialized data only. QuickJS values never cross threads.
struct WorkerChannel {
  std::mutex mutex;
  std::condition_variable wake;
  std::deque<WorkerMessage> incoming, outgoing;
  std::size_t incoming_bytes = 0, outgoing_bytes = 0;
  std::shared_ptr<std::atomic<bool>> stopped = std::make_shared<std::atomic<bool>>(false);
  std::atomic<bool> busy{true};
  std::thread thread;
  std::string url, base, origin, source, name, referrer_policy;
  bool source_supplied = false, module = false;
  bool secure_context = false;
  void enqueue(bool to_worker, WorkerMessage message);
  void stop() noexcept;
  ~WorkerChannel() { stop(); }
};
}  // namespace webcli
