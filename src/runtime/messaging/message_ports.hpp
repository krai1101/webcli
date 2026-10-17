#pragma once
#include <quickjs.h>

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <vector>

#include "runtime/events/task_scheduler.hpp"

namespace webcli {
struct ScriptContext;
// Ports move only between frame realms on the same engine thread. Messages
// contain serialized bytes, never references into another JavaScript context.
class MessagePorts {
 public:
  explicit MessagePorts(ScriptContext& owner) : owner_(owner) {}
  ~MessagePorts();
  JSValue create();
  void send(int id, JSValueConst data);
  void start(int id);
  void close(int id);
  std::vector<int> transfer_to(MessagePorts& receiver, const std::vector<int>& ids);

 private:
  struct Endpoint {
    std::weak_ptr<Endpoint> peer;
    std::deque<std::vector<uint8_t>> messages;
    MessagePorts* owner = nullptr;
    int id = 0;
    bool started = false;
    std::size_t bytes = 0;
    TaskScheduler::TaskId task = 0;
  };
  int attach(const std::shared_ptr<Endpoint>&);
  void schedule(const std::shared_ptr<Endpoint>&);
  ScriptContext& owner_;
  int sequence_ = 0;
  std::map<int, std::shared_ptr<Endpoint>> ports_;
};
JSValue js_message_port(JSContext*, JSValueConst, int, JSValueConst*);
}  // namespace webcli
