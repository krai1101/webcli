#pragma once
#include <quickjs.h>

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace webcli {
struct ScriptContext;
struct BroadcastInbox {
  std::string origin, name;
  std::mutex mutex;
  std::deque<std::shared_ptr<const std::vector<uint8_t>>> messages;
  std::size_t bytes = 0;
  bool closed = false, scheduled = false, opaque = false;
  std::function<void()> wake;
};
// The broker contains serialized bytes and weak subscriptions only. It never
// retains a realm or moves QuickJS values between document/worker threads.
class BroadcastChannels {
 public:
  explicit BroadcastChannels(ScriptContext& owner) : owner_(owner) {}
  ~BroadcastChannels();
  int create(const std::string& name);
  void close(int id);
  void send(int id, JSValueConst message);
  void pump();
  bool pending() const;

 private:
  ScriptContext& owner_;
  int next_id_ = 0;
  std::map<int, std::shared_ptr<BroadcastInbox>> channels_;
};
JSValue js_broadcast_channel(JSContext*, JSValueConst, int, JSValueConst*);
}  // namespace webcli
