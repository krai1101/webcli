#include "runtime/messaging/message_ports.hpp"

#include <limits>
#include <set>

#include "core/resource_limits.hpp"
#include "runtime/engine/diagnostics.hpp"
#include "runtime/engine/js_handles.hpp"
#include "runtime/engine/script_context.hpp"
#include "runtime/workers/worker_manager.hpp"

namespace webcli {
MessagePorts::~MessagePorts() {
  while (!ports_.empty()) close(ports_.begin()->first);
}
int MessagePorts::attach(const std::shared_ptr<Endpoint>& endpoint) {
  if (sequence_ == std::numeric_limits<int>::max()) throw std::bad_alloc();
  const int id = ++sequence_;
  ports_.emplace(id, endpoint);
  endpoint->owner = this;
  endpoint->id = id;
  return id;
}
JSValue MessagePorts::create() {
  auto first = std::make_shared<Endpoint>(), second = std::make_shared<Endpoint>();
  first->peer = second;
  second->peer = first;
  const int one = attach(first);
  int two;
  try {
    two = attach(second);
  } catch (...) {
    close(one);
    throw;
  }
  JSValue result = JS_NewArray(owner_.context);
  if (JS_IsException(result) ||
      JS_SetPropertyUint32(owner_.context, result, 0, JS_NewInt32(owner_.context, one)) < 0 ||
      JS_SetPropertyUint32(owner_.context, result, 1, JS_NewInt32(owner_.context, two)) < 0) {
    JS_FreeValue(owner_.context, result);
    close(one);
    close(two);
    return JS_EXCEPTION;
  }
  return result;
}
void MessagePorts::send(int id, JSValueConst data) {
  auto bytes = serialize_worker_message(owner_.context, data);
  const auto found = ports_.find(id);
  if (found == ports_.end()) return;
  const auto receiver = found->second->peer.lock();
  if (!receiver || !receiver->owner) return;
  if (bytes.size() > kBufferedBodyBytes - receiver->bytes)
    throw CliError("MESSAGE_QUEUE_FULL", "port messages exceed the buffered resource limit");
  receiver->messages.push_back(std::move(bytes));
  receiver->bytes += receiver->messages.back().size();
  receiver->owner->schedule(receiver);
}
void MessagePorts::start(int id) {
  const auto found = ports_.find(id);
  if (found == ports_.end()) return;
  found->second->started = true;
  schedule(found->second);
}
void MessagePorts::close(int id) {
  const auto found = ports_.find(id);
  if (found == ports_.end()) return;
  auto endpoint = found->second;
  if (endpoint->task) owner_.scheduler.cancel(endpoint->task);
  endpoint->task = 0;
  if (auto peer = endpoint->peer.lock()) peer->peer.reset();
  endpoint->peer.reset();
  endpoint->owner = nullptr;
  endpoint->messages.clear();
  endpoint->bytes = 0;
  ports_.erase(found);
}
void MessagePorts::schedule(const std::shared_ptr<Endpoint>& endpoint) {
  if (!endpoint->started || endpoint->task || endpoint->messages.empty()) return;
  endpoint->task = owner_.scheduler.schedule(TaskScheduler::Source::Resource, {}, [this, endpoint] {
    endpoint->task = 0;
    auto bytes = std::move(endpoint->messages.front());
    endpoint->messages.pop_front();
    endpoint->bytes -= bytes.size();
    JsValueOwner data(owner_.context, JS_ReadObject(owner_.context, bytes.data(), bytes.size(),
                                                    JS_READ_OBJ_REFERENCE));
    const bool failed = JS_IsException(data.get());
    if (failed) JS_FreeValue(owner_.context, JS_GetException(owner_.context));
    JsValueOwner global(owner_.context, JS_GetGlobalObject(owner_.context));
    JsValueOwner deliver(owner_.context,
                         JS_GetPropertyStr(owner_.context, global.get(), "__webcliDeliverPort"));
    JSValue args[]{JS_NewInt32(owner_.context, endpoint->id), failed ? JS_UNDEFINED : data.get(),
                   JS_NewBool(owner_.context, failed)};
    JsValueOwner result(owner_.context,
                        JS_Call(owner_.context, deliver.get(), JS_UNDEFINED, 3, args));
    if (JS_IsException(result.get())) record_async_js_error(&owner_, owner_.context);
    // A handler may close or transfer the port while receiving this message.
    if (endpoint->owner == this) schedule(endpoint);
  });
}
std::vector<int> MessagePorts::transfer_to(MessagePorts& receiver, const std::vector<int>& ids) {
  owner_.require_owner();
  receiver.owner_.require_owner();
  if (owner_.runtime != receiver.owner_.runtime)
    throw CliError("PORT_TRANSFER", "port transfers between worker runtimes are unsupported");
  std::set<int> unique;
  for (int id : ids)
    if (!ports_.count(id) || !unique.insert(id).second)
      throw CliError("PORT_TRANSFER", "transfer contains a detached or duplicate port");
  std::vector<int> adopted;
  adopted.reserve(ids.size());
  // Allocate every receiver entry before detaching any sender endpoint.
  std::vector<std::shared_ptr<Endpoint>> endpoints;
  for (int id : ids) endpoints.push_back(ports_.at(id));
  const int previous = receiver.sequence_;
  try {
    for (const auto& endpoint : endpoints) {
      if (receiver.sequence_ == std::numeric_limits<int>::max()) throw std::bad_alloc();
      const int id = ++receiver.sequence_;
      receiver.ports_.emplace(id, endpoint);
      adopted.push_back(id);
    }
  } catch (...) {
    for (int id : adopted) receiver.ports_.erase(id);
    receiver.sequence_ = previous;
    throw;
  }
  for (std::size_t index = 0; index < ids.size(); ++index) {
    auto& endpoint = endpoints[index];
    if (endpoint->task) owner_.scheduler.cancel(endpoint->task);
    endpoint->task = 0;
    endpoint->started = false;
    endpoint->owner = &receiver;
    endpoint->id = adopted[index];
    ports_.erase(ids[index]);
  }
  return adopted;
}
JSValue js_message_port(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  auto& state = *static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  if (!state.ports) state.ports = std::make_shared<MessagePorts>(state);
  if (!argc) return JS_ThrowTypeError(context, "port operation required");
  const auto operation = js_string(context, argv[0]);
  if (operation == "create") return state.ports->create();
  int32_t id;
  if (argc < 2) return JS_ThrowTypeError(context, "port ID required");
  if (JS_ToInt32(context, &id, argv[1]) < 0) return JS_EXCEPTION;
  if (operation == "start")
    state.ports->start(id);
  else if (operation == "close")
    state.ports->close(id);
  else if (operation == "send" && argc > 2) {
    try {
      state.ports->send(id, argv[2]);
    } catch (const CliError& error) {
      if (error.code != "WORKER_CLONE") throw;
      if (JS_HasException(context)) JS_FreeValue(context, JS_GetException(context));
      return JS_ThrowDOMException(context, "DataCloneError", "%s", error.what());
    }
  } else
    return JS_ThrowTypeError(context, "invalid port operation");
  return JS_UNDEFINED;
}
}  // namespace webcli
