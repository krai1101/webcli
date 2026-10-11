#pragma once
#include <quickjs.h>

#include <memory>
#include <unordered_map>

#include "runtime/workers/worker_channel.hpp"
namespace webcli {
struct ScriptContext;
class WorkerManager {
 public:
  explicit WorkerManager(ScriptContext& owner) : owner_(owner) {}
  ~WorkerManager();
  void cancel_all();
  int create(const std::string& url, bool module, const std::string& name);
  void send(int id, std::vector<uint8_t> bytes);
  void terminate(int id);
  void pump();
  bool pending() const;

 private:
  ScriptContext& owner_;
  int next_id_ = 0;
  std::unordered_map<int, std::shared_ptr<WorkerChannel>> workers_;
};
JSValue js_worker_access(JSContext*, JSValueConst, int, JSValueConst*);
std::vector<uint8_t> serialize_worker_message(JSContext*, JSValueConst);
void run_worker(WorkerChannel&) noexcept;
}  // namespace webcli
