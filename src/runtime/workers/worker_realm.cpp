#include <chrono>

#include "core/browser_identity.hpp"
#include "network/http_client.hpp"
#include "runtime/crypto/aes_gcm_bindings.hpp"
#include "runtime/crypto/digest_bindings.hpp"
#include "runtime/embedded_scripts.hpp"
#include "runtime/engine/diagnostics.hpp"
#include "runtime/engine/js_handles.hpp"
#include "runtime/engine/native_guard.hpp"
#include "runtime/engine/script_context.hpp"
#include "runtime/events/timers.hpp"
#include "runtime/intl/number_format_bindings.hpp"
#include "runtime/intl/segmenter_bindings.hpp"
#include "runtime/navigation/history_clone_bindings.hpp"
#include "runtime/network/blob_urls.hpp"
#include "runtime/network/completion_delivery.hpp"
#include "runtime/network/resource_tasks.hpp"
#include "runtime/network/web_bindings.hpp"
#include "runtime/url/url_bindings.hpp"
#include "runtime/workers/worker_manager.hpp"
#include "runtime/messaging/broadcast_channels.hpp"
namespace webcli {
namespace {
int interrupt_worker(JSRuntime*, void* opaque) {
  return static_cast<ScriptContext*>(opaque)->execution.interrupt();
}
class WorkerRealm {
 public:
  ScriptContext state;
  std::vector<std::string> errors;
  explicit WorkerRealm(WorkerChannel& channel) {
    state.worker_channel = &channel;
    state.cancelled = channel.stopped;
    state.execution.set_cancellation(channel.stopped);
    state.errors = &errors;
    state.base_url = channel.base;
    state.security_origin = channel.origin;
    state.referrer_policy = channel.referrer_policy;
  }
  ~WorkerRealm() {
    state.active = false;
    if (!state.runtime) return;
    if (state.context) {
      clear_promise_rejections(state);
      if (state.modules) state.modules->clear_compiled(state.context);
      for (auto& module : state.pending_modules) JS_FreeValue(state.context, module.promise);
      for (auto& job : state.network.fetches)
        if (job->cancelled) job->cancelled->store(true);
      state.network.fetches.clear();
      for (auto& timer : state.timers) {
        JS_FreeValue(state.context, timer.second->callback);
        delete timer.second;
      }
      state.timers.clear();
      state.scheduler.clear();
      state.broadcasts.reset();
      JS_FreeContext(state.context);
    }
    JS_SetHostPromiseRejectionTracker(state.runtime, nullptr, nullptr);
    JS_RunGC(state.runtime);
    JS_FreeRuntime(state.runtime);
  }
  bool evaluate(const std::string& source, const std::string& name, bool module = false) {
    state.error_context = name;
    ExecutionBudget::Window window(state.execution);
    if (!begin_js_task(&state)) return false;
    JsValueOwner result(state.context, module ? state.modules->evaluate(state.context, source, name)
                                              : JS_Eval(state.context, source.data(), source.size(),
                                                        name.c_str(), JS_EVAL_TYPE_GLOBAL));
    if (JS_IsException(result.get())) {
      record_async_js_error(&state, state.context);
      return false;
    }
    if (module && JS_PromiseState(state.context, result.get()) == JS_PROMISE_PENDING)
      state.pending_modules.push_back({JS_DupValue(state.context, result.get()), name, false});
    drain_timer_jobs(&state);
    return true;
  }
  void initialize(WorkerChannel& channel) {
    state.runtime = JS_NewRuntime();
    if (!state.runtime) throw CliError("WORKER_INIT", "cannot create worker runtime");
    JS_SetMaxStackSize(state.runtime, kJsStackBytes);
    JS_SetInterruptHandler(state.runtime, interrupt_worker, &state);
    state.context = JS_NewContext(state.runtime);
    if (!state.context || JS_AddIntrinsicDOMException(state.context) < 0)
      throw CliError("WORKER_INIT", "cannot create worker context");
    JS_SetContextOpaque(state.context, &state);
    JS_SetHostPromiseRejectionTracker(state.runtime, promise_rejection_tracker, nullptr);
    state.modules = std::make_unique<ModuleLoader>(state.runtime, state.base_url, &state.execution,
                                                   &state.script_loading, &state.referrer_policy,
                                                   state.cancelled);
    install_segmenter_class(state.context);
    install_number_format_class(state.context);
    JsValueOwner global(state.context, JS_GetGlobalObject(state.context));
    struct Binding {
      const char* name;
      JSCFunction* function;
      int argc;
    };
    const Binding bindings[] = {
        {"__webcliBroadcast", guarded_binding<js_broadcast_channel>, 3},
      {"__webcliWorker", guarded_binding<js_worker_access>, 4},
        {"__webcliURLParse", guarded_binding<js_url_parse>, 4},
        {"__webcliSegmenter", guarded_binding<js_segmenter>, 4},
        {"__webcliNumberFormat", guarded_binding<js_number_format>, 3},
        {"__webcliIsProxy", guarded_binding<js_is_proxy>, 1},
        {"__webcliReportError", guarded_binding<js_report_error>, 2},
        {"__webcliNow", guarded_binding<js_performance_now>, 0},
        {"__webcliRandomBytes", guarded_binding<js_native_random_bytes>, 1},
        {"__webcliCryptoDigest", guarded_binding<js_crypto_digest>, 2},
        {"__webcliCryptoAESGCM", guarded_binding<js_crypto_aes_gcm>, 6},
        {"__webcliRegisterBlobURL", guarded_binding<js_blob_url_register>, 2},
        {"__webcliRevokeBlobURL", guarded_binding<js_blob_url_revoke>, 1},
        {"__webcliFetch", guarded_binding<js_native_fetch>, 2},
        {"__webcliCancelFetch", guarded_binding<js_native_cancel_fetch>, 1},
        {"setTimeout", guarded_binding<js_native_set_timeout>, 2},
        {"clearTimeout", guarded_binding<js_native_clear_timeout>, 1},
        {"queueMicrotask", guarded_binding<js_native_queue_microtask>, 1},
    };
    for (const auto& binding : bindings)
      if (JS_SetPropertyStr(
              state.context, global.get(), binding.name,
              JS_NewCFunction(state.context, binding.function, binding.name, binding.argc)) < 0)
        throw CliError("WORKER_INIT", "cannot install worker binding");
    const auto property = [&](const char* key, const std::string& value) {
      if (JS_SetPropertyStr(state.context, global.get(), key,
                            JS_NewString(state.context, value.c_str())) < 0)
        throw CliError("WORKER_INIT", "cannot initialize worker global");
    };
    property("__webcliCurrentURL", channel.url);
    property("__webcliWorkerName", channel.name);
    property("__webcliUserAgent", kUserAgent);
    property("__webcliBrowserBrand", kBrowserBrand);
    property("__webcliBrowserMajorVersion", kBrowserMajorVersion);
    property("__webcliBrowserVersion", kBrowserVersion);
#ifdef _WIN32
    property("__webcliPlatform", "Win32");
#elif defined(__APPLE__)
    property("__webcliPlatform", "MacIntel");
#else
    property("__webcliPlatform", "Linux");
#endif
    if (!evaluate("const __webcliLimits=Object.freeze({bufferedBodyBytes:" +
                      std::to_string(kBufferedBodyBytes) +
                      ",timerDelayMs:" + std::to_string(kTimerDelayMs) + "});",
                  "worker limits") ||
        !evaluate(kWorkerApiSource, "webcli:worker-api"))
      throw CliError("WORKER_INIT", errors.empty() ? "worker bootstrap failed" : errors.back());
  }
  void pump() {
    ExecutionBudget::Window window(state.execution);
    drain_timer_jobs(&state);
    if (state.broadcasts) state.broadcasts->pump();
    start_queued_http(state);
    for (auto it = state.network.fetches.begin(); it != state.network.fetches.end();) {
      if (!(*it)->future.valid() ||
          (*it)->future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        ++it;
        continue;
      }
      auto job = std::move(*it);
      it = state.network.fetches.erase(it);
      schedule_resource_completion(state, std::move(job));
    }
    const auto end = TaskScheduler::Clock::now() + std::chrono::milliseconds(8);
    while (state.scheduler.has_ready() && TaskScheduler::Clock::now() < end &&
           begin_js_task(&state)) {
      state.scheduler.run_one();
      drain_timer_jobs(&state);
    }
    if (!JS_IsJobPending(state.runtime)) flush_promise_rejections(state);
    for (auto it = state.pending_modules.begin(); it != state.pending_modules.end();) {
      if (JS_PromiseState(state.context, it->promise) == JS_PROMISE_PENDING) {
        ++it;
        continue;
      }
      JS_FreeValue(state.context, it->promise);
      it = state.pending_modules.erase(it);
    }
  }
  void receive(const WorkerMessage& message) {
    if (!begin_js_task(&state)) return;
    JsValueOwner data(state.context, JS_ReadObject(state.context, message.bytes.data(),
                                                   message.bytes.size(), JS_READ_OBJ_REFERENCE));
    if (JS_IsException(data.get())) {
      record_async_js_error(&state, state.context);
      return;
    }
    JsValueOwner global(state.context, JS_GetGlobalObject(state.context));
    JsValueOwner deliver(state.context, JS_GetPropertyStr(state.context, global.get(),
                                                          "__webcliDeliverWorkerMessage"));
    JSValue argument = data.get();
    JsValueOwner result(state.context,
                        JS_Call(state.context, deliver.get(), JS_UNDEFINED, 1, &argument));
    if (JS_IsException(result.get())) record_async_js_error(&state, state.context);
    drain_timer_jobs(&state);
  }
};
}  // namespace
void run_worker(WorkerChannel& channel) noexcept {
  struct Completion {
    WorkerChannel& channel;
    ~Completion() {
      std::lock_guard<std::mutex> lock(channel.mutex);
      channel.stopped->store(true);
      channel.incoming.clear();
      channel.incoming_bytes = 0;
      channel.busy.store(false);
    }
  } completion{channel};
  try {
    WorkerRealm realm(channel);
    if (!channel.source_supplied) {
      HttpRequest request;
      request.url = channel.url;
      request.allowed_origin = channel.origin;
      request.owner_cancelled = channel.stopped;
      request.referrer_policy = channel.referrer_policy;
      request.max_body_bytes = kMaxScriptBytes;
      auto response = load_script_resource(request);
      if (response.status < 200 || response.status >= 300)
        throw CliError("WORKER_HTTP_STATUS",
                       "worker script returned HTTP " + std::to_string(response.status));
      channel.source = std::move(response.body);
      channel.base = response.url;
      realm.state.base_url = channel.base;
    }
    realm.initialize(channel);
    if (!realm.evaluate(channel.source, channel.url, channel.module)) {
      for (const auto& error : realm.errors) channel.enqueue(false, {{}, error});
      return;
    }
    channel.source.clear();
    channel.source.shrink_to_fit();
    while (!channel.stopped->load()) {
      std::deque<WorkerMessage> messages;
      {
        std::lock_guard<std::mutex> lock(channel.mutex);
        channel.busy.store(true);
        if (realm.state.pending_modules.empty()) {
          messages.swap(channel.incoming);
          channel.incoming_bytes = 0;
        }
      }
      for (const auto& message : messages) {
        if (channel.stopped->load()) break;
        realm.receive(message);
      }
      realm.pump();
      for (const auto& error : realm.errors) channel.enqueue(false, {{}, error});
      realm.errors.clear();
      const bool pending =
          !realm.state.network.fetches.empty() || JS_IsJobPending(realm.state.runtime) ||
          !realm.state.pending_modules.empty() || realm.state.modules->has_pending_sources() ||
          realm.state.scheduler.has_ready() ||
          (realm.state.broadcasts && realm.state.broadcasts->pending());
      std::unique_lock<std::mutex> lock(channel.mutex);
      channel.busy.store(pending);
      auto deadline = realm.state.scheduler.next_deadline();
      if (pending) deadline = TaskScheduler::Clock::now() + std::chrono::milliseconds(1);
      const auto ready = [&] {
        return channel.stopped->load() ||
               (realm.state.broadcasts && realm.state.broadcasts->pending()) ||
               (realm.state.pending_modules.empty() && !channel.incoming.empty());
      };
      if (deadline)
        channel.wake.wait_until(lock, *deadline, ready);
      else
        channel.wake.wait(lock, ready);
    }
  } catch (const std::exception& error) {
    try {
      channel.enqueue(false, {{}, error.what()});
    } catch (...) {
    }
  } catch (...) {
    try {
      channel.enqueue(false, {{}, "worker runtime failed"});
    } catch (...) {
    }
  }
  channel.busy.store(false);
}
}  // namespace webcli
