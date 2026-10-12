#pragma once
#include <quickjs.h>

#include <atomic>
#include <chrono>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include "network/http_client.hpp"
#include "runtime/engine/execution_budget.hpp"
#include "runtime/scripts/import_map.hpp"
#include "runtime/scripts/module_contexts.hpp"
#include "runtime/scripts/script_loading.hpp"

namespace webcli {
// URL-based modules share document loading deadlines and transport policy.
class ModuleLoader {
 public:
  ModuleLoader(JSRuntime*, std::string base_url, std::string security_origin,
               ExecutionBudget* execution, ScriptLoading* loading,
               const std::string* referrer_policy,
               std::shared_ptr<std::atomic<bool>> cancelled = {});
  JSValue evaluate(JSContext*, const std::string& source, const std::string& url);
  void add_import_map(JSContext*, const std::string& source);
  ~ModuleLoader();
  void clear_compiled(JSContext*);
  void preload(const std::string& url);
  std::string source(const std::string& url);
  bool has_pending_sources() const;
  void remember_script(const std::string& id, const std::string& base, NavigationReferrer referrer,
                       bool module = true, bool credentials = false);
  void received_script(const std::string& id, const HttpResponse& response);
  void finish_initial_loading() { initial_loading_ = false; }
  void begin_turn() {
    if (!initial_loading_) loading_->begin();
  }

 private:
  std::string base_url_;
  std::string security_origin_;
  const std::string* referrer_policy_;
  ImportMap import_map_;
  ModuleContexts contexts_;
  ScriptLoading* loading_;
  ExecutionBudget* execution_;
  bool initial_loading_ = true;
  struct Source {
    std::future<HttpResponse> transfer;
    std::shared_ptr<std::atomic<bool>> cancelled;
    std::optional<std::string> text;
    std::exception_ptr failure;
  };
  std::map<std::string, Source> sources_;
  std::map<std::string, JSValue> compiled_;
  std::shared_ptr<std::atomic<bool>> cancelled_;
  void release_source(const std::string&);
  void preload_dependencies(const std::string&, const std::string&);
  static char* normalize(JSContext*, const char*, const char*, void*);
  static JSModuleDef* load(JSContext*, const char*, void*);
  bool import_meta(JSContext*, JSModuleDef*, const std::string&);
};
}  // namespace webcli
