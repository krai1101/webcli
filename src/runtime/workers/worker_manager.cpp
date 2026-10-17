#include "runtime/workers/worker_manager.hpp"

#include <limits>

#include "core/resource_limits.hpp"
#include "core/url/url.hpp"
#include "runtime/engine/diagnostics.hpp"
#include "runtime/engine/js_handles.hpp"
#include "runtime/engine/script_context.hpp"
namespace webcli {
void WorkerChannel::enqueue(bool to_worker, WorkerMessage message) {
  std::lock_guard<std::mutex> lock(mutex);
  if (stopped->load()) return;
  auto& bytes = to_worker ? incoming_bytes : outgoing_bytes;
  if (message.bytes.size() + message.error.size() > kBufferedBodyBytes - bytes)
    throw CliError("WORKER_QUEUE_FULL", "worker messages exceed the buffered resource limit");
  const auto size = message.bytes.size() + message.error.size();
  (to_worker ? incoming : outgoing).push_back(std::move(message));
  bytes += size;
  wake.notify_one();
}
void WorkerChannel::stop() noexcept {
  stopped->store(true);
  wake.notify_one();
  if (thread.joinable()) thread.join();
}
WorkerManager::~WorkerManager() { cancel_all(); }
void WorkerManager::cancel_all() {
  // Cancel every worker before joining any of them.
  for (auto& entry : workers_) {
    entry.second->stopped->store(true);
    entry.second->wake.notify_one();
  }
  for (auto& entry : workers_) entry.second->stop();
  workers_.clear();
}
int WorkerManager::create(const std::string& url, bool module, const std::string& name) {
  owner_.require_owner();
  if (!owner_.scripting_allowed || owner_.opaque_origin)
    throw CliError("WORKER_SECURITY", "workers require a script-enabled, nonopaque origin");
  if (next_id_ == std::numeric_limits<int>::max())
    throw CliError("WORKER_ID", "worker ID sequence exhausted");
  auto channel = std::make_shared<WorkerChannel>();
  channel->url = url;
  channel->base = url;
  channel->origin = owner_.security_origin;
  channel->secure_context = owner_.secure_context;
  channel->module = module;
  channel->name = name;
  channel->referrer_policy = owner_.referrer_policy;
  if (url.rfind("blob:", 0) == 0) {
    const auto source = owner_.network.blob_urls.find(url);
    if (source == owner_.network.blob_urls.end())
      throw CliError("WORKER_SECURITY", "worker Blob URL is absent or revoked");
    channel->source = source->second;
    channel->source_supplied = true;
  } else if (url_origin(url) != owner_.security_origin ||
             (url.rfind("https://", 0) != 0 && url.rfind("http://", 0) != 0)) {
    throw CliError("WORKER_SECURITY",
                   "worker script must use a same-origin HTTP(S) or owned Blob URL");
  }
  const int id = ++next_id_;
  workers_.emplace(id, channel);
  try {
    channel->thread = std::thread([pointer = channel.get()] { run_worker(*pointer); });
  } catch (...) {
    workers_.erase(id);
    throw;
  }
  return id;
}
void WorkerManager::send(int id, std::vector<uint8_t> bytes) {
  const auto found = workers_.find(id);
  if (found != workers_.end()) found->second->enqueue(true, {std::move(bytes), {}});
}
void WorkerManager::terminate(int id) {
  const auto found = workers_.find(id);
  if (found == workers_.end()) return;
  found->second->stop();
  workers_.erase(found);
}
bool WorkerManager::pending() const {
  for (const auto& item : workers_) {
    const auto& channel = *item.second;
    std::lock_guard<std::mutex> lock(item.second->mutex);
    if (channel.busy.load() || !channel.incoming.empty() || !channel.outgoing.empty()) return true;
  }
  return false;
}
void WorkerManager::pump() {
  owner_.require_owner();
  for (auto& entry : workers_) {
    std::deque<WorkerMessage> messages;
    {
      std::lock_guard<std::mutex> lock(entry.second->mutex);
      messages.swap(entry.second->outgoing);
      entry.second->outgoing_bytes = 0;
    }
    for (auto& message : messages) {
      const int id = entry.first;
      // Deliver on the creating realm's task queue, never on the worker thread.
      auto channel = entry.second;
      owner_.scheduler.schedule(
          TaskScheduler::Source::Resource, {}, [this, id, channel, message = std::move(message)] {
            if (!workers_.count(id)) return;
            JsValueOwner global(owner_.context, JS_GetGlobalObject(owner_.context));
            JsValueOwner deliver(owner_.context, JS_GetPropertyStr(owner_.context, global.get(),
                                                                   "__webcliDeliverWorker"));
            JSValue data = message.bytes.empty()
                               ? JS_UNDEFINED
                               : JS_ReadObject(owner_.context, message.bytes.data(),
                                               message.bytes.size(), JS_READ_OBJ_REFERENCE);
            if (JS_IsException(data)) {
              record_async_js_error(&owner_, owner_.context);
              return;
            }
            JSValue args[] = {JS_NewInt32(owner_.context, id), data,
                              JS_NewString(owner_.context, message.error.c_str())};
            JsValueOwner result(owner_.context,
                                JS_Call(owner_.context, deliver.get(), JS_UNDEFINED, 3, args));
            for (auto arg : args) JS_FreeValue(owner_.context, arg);
            if (JS_IsException(result.get())) record_async_js_error(&owner_, owner_.context);
            if (!message.error.empty() && owner_.errors->size() < kMaxScriptErrors)
              owner_.errors->push_back("worker " + channel->url + ": " + message.error);
          });
    }
  }
}
}  // namespace webcli
