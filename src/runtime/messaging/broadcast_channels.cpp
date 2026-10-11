#include "runtime/messaging/broadcast_channels.hpp"

#include <limits>
#include <algorithm>

#include "core/resource_limits.hpp"
#include "runtime/engine/diagnostics.hpp"
#include "runtime/engine/js_handles.hpp"
#include "runtime/engine/script_context.hpp"
#include "runtime/workers/worker_manager.hpp"

namespace webcli {
namespace {
std::mutex broker_mutex;
std::vector<std::weak_ptr<BroadcastInbox>> subscribers;
void prune_subscribers() {
  subscribers.erase(std::remove_if(subscribers.begin(), subscribers.end(),
      [](const auto& subscriber) { return subscriber.expired(); }), subscribers.end());
}
}  // namespace
BroadcastChannels::~BroadcastChannels() {
  while (!channels_.empty()) close(channels_.begin()->first);
  std::lock_guard lock(broker_mutex);
  prune_subscribers();
}
int BroadcastChannels::create(const std::string& name) {
  owner_.require_owner();
  if (next_id_ == std::numeric_limits<int>::max())
    throw CliError("BROADCAST_ID", "broadcast channel ID sequence exhausted");
  auto inbox = std::make_shared<BroadcastInbox>();
  inbox->origin = owner_.security_origin;
  inbox->name = name;
  inbox->opaque = owner_.opaque_origin;
  if (owner_.worker_channel) {
    auto* worker = static_cast<WorkerChannel*>(owner_.worker_channel);
    inbox->wake = [worker] { worker->wake.notify_one(); };
  }
  const int id = ++next_id_;
  channels_.emplace(id, inbox);
  try {
    std::lock_guard lock(broker_mutex);
    prune_subscribers();
    subscribers.emplace_back(inbox);
  } catch (...) {
    channels_.erase(id);
    throw;
  }
  return id;
}
void BroadcastChannels::close(int id) {
  const auto found = channels_.find(id);
  if (found == channels_.end()) return;
  {
    std::lock_guard lock(found->second->mutex);
    found->second->closed = true;
    found->second->messages.clear();
    found->second->bytes = 0;
  }
  channels_.erase(found);
}
void BroadcastChannels::send(int id, JSValueConst message) {
  owner_.require_owner();
  const auto sender = channels_.find(id);
  if (sender == channels_.end())
    throw CliError("BROADCAST_CLOSED", "broadcast channel is closed");
  auto bytes = std::make_shared<const std::vector<uint8_t>>(
      serialize_worker_message(owner_.context, message));
  if (owner_.opaque_origin) return;  // Opaque origins never match another realm.
  std::lock_guard broker_lock(broker_mutex);
  for (const auto& subscription : subscribers) {
    auto target = subscription.lock();
    if (!target || target == sender->second || target->origin != sender->second->origin ||
        target->name != sender->second->name) continue;
    std::lock_guard lock(target->mutex);
    if (target->closed || target->opaque) continue;
    if (bytes->size() > kBufferedBodyBytes - target->bytes)
      throw CliError("BROADCAST_QUEUE_FULL", "broadcast messages exceed the buffered resource limit");
    target->messages.push_back(bytes);
    target->bytes += bytes->size();
    if (target->wake) target->wake();
  }
}
void BroadcastChannels::pump() {
  owner_.require_owner();
  for (const auto& [id, inbox] : channels_) {
    std::lock_guard lock(inbox->mutex);
    if (inbox->scheduled || inbox->messages.empty()) continue;
    owner_.scheduler.schedule(TaskScheduler::Source::Resource, {}, [this, id, inbox] {
      std::shared_ptr<const std::vector<uint8_t>> message;
      {
        std::lock_guard delivery_lock(inbox->mutex);
        inbox->scheduled = false;
        if (inbox->closed || inbox->messages.empty()) return;
        message = std::move(inbox->messages.front());
        inbox->messages.pop_front();
        inbox->bytes -= message->size();
      }
      JsValueOwner data(owner_.context, JS_ReadObject(owner_.context, message->data(),
                                                     message->size(), JS_READ_OBJ_REFERENCE));
      const bool failed = JS_IsException(data.get());
      if (failed) {
        JsValueOwner exception(owner_.context, JS_GetException(owner_.context));
      }
      JsValueOwner global(owner_.context, JS_GetGlobalObject(owner_.context));
      JsValueOwner deliver(owner_.context, JS_GetPropertyStr(owner_.context, global.get(),
                                                            "__webcliDeliverBroadcast"));
      JSValue args[]{JS_NewInt32(owner_.context, id),
                     failed ? JS_UNDEFINED : data.get(), JS_NewBool(owner_.context, failed)};
      JsValueOwner result(owner_.context, JS_Call(owner_.context, deliver.get(),
                                                 global.get(), 3, args));
      if (JS_IsException(result.get())) record_async_js_error(&owner_, owner_.context);
    });
    inbox->scheduled = true;
  }
}
bool BroadcastChannels::pending() const {
  for (const auto& [id, inbox] : channels_) {
    std::lock_guard lock(inbox->mutex);
    if (inbox->scheduled || !inbox->messages.empty()) return true;
  }
  return false;
}
JSValue js_broadcast_channel(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  auto& state = *static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  if (!state.broadcasts) state.broadcasts = std::make_shared<BroadcastChannels>(state);
  if (argc < 2) return JS_ThrowTypeError(context, "broadcast operation required");
  const auto operation = js_string(context, argv[0]);
  if (operation == "create") return JS_NewInt32(context, state.broadcasts->create(js_string(context, argv[1])));
  int32_t id;
  if (JS_ToInt32(context, &id, argv[1]) < 0) return JS_EXCEPTION;
  if (operation == "close") state.broadcasts->close(id);
  else if (operation == "send" && argc > 2) {
    try { state.broadcasts->send(id, argv[2]); }
    catch (const CliError& error) {
      if (error.code == "WORKER_CLONE") {
        JsValueOwner exception(context, JS_GetException(context));
        return JS_ThrowDOMException(context, "DataCloneError", "%s", error.what());
      }
      if (error.code == "BROADCAST_CLOSED")
        return JS_ThrowDOMException(context, "InvalidStateError", "%s", error.what());
      throw;
    }
  } else return JS_ThrowTypeError(context, "invalid broadcast operation");
  return JS_UNDEFINED;
}
}  // namespace webcli
