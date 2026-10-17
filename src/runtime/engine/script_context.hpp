#pragma once
#include <lexbor/html/html.h>
#include <quickjs.h>

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "core/errors.hpp"
#include "core/page.hpp"
#include "runtime/css/style_state.hpp"
#include "runtime/dom/dom_state.hpp"
#include "runtime/engine/execution_budget.hpp"
#include "runtime/events/task_scheduler.hpp"
#include "runtime/navigation/history_action.hpp"
#include "runtime/navigation/navigation_action.hpp"
#include "runtime/network/resource_state.hpp"
#include "runtime/page/document_lifecycle.hpp"
#include "runtime/scripts/module_loader.hpp"
#include "runtime/scripts/script_loading.hpp"
#include "runtime/storage/indexed_db_store.hpp"
#include "storage/profile_store.hpp"

namespace webcli {
struct WasmScope;
class FrameTree;
class WorkerManager;
class BroadcastChannels;
class MessagePorts;
struct JsTimer;
struct PendingRejection {
  JSValue promise, reason;
  std::string origin;
};
struct PendingModule {
  JSValue promise;
  std::string origin;
  bool blocks_content_loaded = true;
};
struct ScriptContext : std::enable_shared_from_this<ScriptContext> {
  std::shared_ptr<WasmScope> wasm;
  std::shared_ptr<WorkerManager> workers;
  std::shared_ptr<BroadcastChannels> broadcasts;
  std::shared_ptr<MessagePorts> ports;
  void* worker_channel = nullptr;
  FrameTree* frame_tree = nullptr;
  std::shared_ptr<FrameTree> frame_owner;
  std::shared_ptr<lxb_html_document_t> frame_document;
  int frame_id = 0;
  std::string security_origin;
  bool opaque_origin = false;
  bool secure_context = false;
  bool scripting_allowed = true;
  std::string referrer_policy;
  std::string document_referrer;
  std::optional<NavigationAction> navigation;
  const std::thread::id owner_thread = std::this_thread::get_id();
  void require_owner() const {
    if (owner_thread != std::this_thread::get_id())
      throw CliError("RUNTIME_THREAD", "document runtime accessed outside its owner thread");
  }
  std::shared_ptr<std::atomic<bool>> cancelled;
  bool active = true;
  DocumentLifecycle lifecycle;
  TaskScheduler scheduler;
  DomState dom;
  StyleState css;
  ResourceState network;
  std::vector<HistoryAction> history_actions;
  std::shared_ptr<HistoryArchive> history_archive;
  std::function<void(std::string)> parser_write;
  std::string base_url;
  // Captured at document creation, never from mutable JavaScript URL state.
  std::string history_source_url;
  StorageStore* storage = nullptr;
  JSRuntime* runtime = nullptr;
  JSContext* context = nullptr;
  JSClassID frame_owner_class = 0;
  JSValue frame_message_delivery = JS_UNDEFINED;
  JSClassID segmenter_class = 0;
  JSClassID number_format_class = 0;
  JSClassID parsed_document_class = 0;
  const std::chrono::steady_clock::time_point time_origin = std::chrono::steady_clock::now();
  ExecutionBudget execution;
  ScriptLoading script_loading;
  std::vector<std::string>* errors = nullptr;
  std::string error_context;
  std::unordered_map<int, std::string> classic_script_contexts;
  int next_timer_id = 1;
  std::unordered_map<int, JsTimer*> timers;
  std::unique_ptr<ModuleLoader> modules;
  std::unique_ptr<IndexedDbStore> indexed_db;
  std::vector<PendingModule> pending_modules;
  std::vector<PendingRejection> rejections;
  std::deque<DiagnosticEntry> diagnostic_trace;
  std::size_t diagnostic_dropped = 0;
};
struct JsTimer {
  TaskScheduler::TaskId task = 0;
  JSValue callback = JS_UNDEFINED;
};
}  // namespace webcli
