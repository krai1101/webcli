#include "runtime/scripts/module_loader.hpp"

#include <algorithm>
#include <cstring>
#include <string_view>
#include <utility>

#include "core/errors.hpp"
#include "core/load_trace.hpp"
#include "core/page.hpp"
#include "network/data_script.hpp"
#include "network/http_client.hpp"
#include "network/resource_service.hpp"
#include "runtime/engine/script_context.hpp"
#include "runtime/scripts/module_requests.hpp"

namespace webcli {
ModuleLoader::~ModuleLoader() {
  for (auto& entry : sources_)
    if (entry.second.cancelled) entry.second.cancelled->store(true);
}
void ModuleLoader::clear_compiled(JSContext* context) {
  for (auto& entry : compiled_) JS_FreeValue(context, entry.second);
  compiled_.clear();
}
void ModuleLoader::release_source(const std::string& url) {
  const auto entry = sources_.find(url);
  if (entry != sources_.end()) entry->second.text.reset();
}
void ModuleLoader::preload_dependencies(const std::string& source, const std::string& id) {
  const auto* parent = contexts_.find(id);
  const auto base = parent ? parent->base_url : id;
  const auto policy = parent ? parent->request.policy : *referrer_policy_;
  for (const auto& specifier : module_requests(source, id, *execution_)) {
    const auto url = import_map_.resolve(specifier, base);
    if (url == id || compiled_.count(url) || sources_.count(url)) continue;
    contexts_.remember(url, url, {base, policy});
    preload(url);
  }
}
void ModuleLoader::preload(const std::string& url) {
  if (url.rfind("blob:", 0) == 0 || is_data_script_url(url) || sources_.count(url)) return;
  HttpRequest request;
  request.url = url;
  request.owner_cancelled = cancelled_;
  request.cookie_origin = base_url_;
  contexts_.remember(url, url, {base_url_, *referrer_policy_});
  const auto* environment = contexts_.find(url);
  request.referrer_source = environment->request.source;
  request.referrer_policy = environment->request.policy;
  request.max_body_bytes = kMaxScriptBytes;
  request.timeout_ms = loading_->request_timeout();
  request.cancelled = std::make_shared<std::atomic<bool>>(false);
  auto future = resource_service().submit([request] { return perform_http(request); });
  sources_.emplace(url, Source{std::move(future), request.cancelled, std::nullopt, {}});
  contexts_.dispatched(url);
}
std::string ModuleLoader::source(const std::string& url) {
  if (compiled_.count(url)) return {};
  if (is_data_script_url(url)) return decode_data_script(url);
  preload(url);
  auto& entry = sources_.at(url);
  if (entry.failure) std::rethrow_exception(entry.failure);
  if (!entry.text) {
    while (entry.transfer.wait_for(std::chrono::milliseconds(10)) != std::future_status::ready) {
      if (cancelled_ && cancelled_->load()) {
        entry.cancelled->store(true);
        throw CliError("REQUEST_CANCELLED", "module loading cancelled");
      }
      try {
        loading_->request_timeout();
      } catch (...) {
        entry.cancelled->store(true);
        entry.failure = std::current_exception();
        throw;
      }
    }
    if (cancelled_ && cancelled_->load()) {
      entry.cancelled->store(true);
      entry.failure = std::make_exception_ptr(
          CliError("REQUEST_CANCELLED", "module loading cancelled: " + url));
      std::rethrow_exception(entry.failure);
    }
    try {
      auto response = entry.transfer.get();
      entry.text = std::move(response.body);
      if (response.status < 200 || response.status >= 300) {
        release_source(url);
        throw CliError("SCRIPT_HTTP_STATUS",
                       "module returned HTTP " + std::to_string(response.status) + ": " + url);
      }
      received_script(url, response);
    } catch (const CliError& error) {
      release_source(url);
      entry.failure = std::make_exception_ptr(
          CliError(error.code, std::string(error.what()) + ": " + url, error.retryable));
      std::rethrow_exception(entry.failure);
    } catch (...) {
      release_source(url);
      entry.failure = std::current_exception();
      throw;
    }
  }
  return *entry.text;
}
bool ModuleLoader::has_pending_sources() const {
  return std::any_of(sources_.begin(), sources_.end(), [](const auto& entry) {
    return entry.second.transfer.valid() &&
           entry.second.transfer.wait_for(std::chrono::milliseconds(0)) !=
               std::future_status::ready;
  });
}
void ModuleLoader::remember_script(const std::string& id, const std::string& base,
                                   NavigationReferrer referrer, bool module) {
  contexts_.remember(id, base, std::move(referrer), module);
}
void ModuleLoader::received_script(const std::string& id, const HttpResponse& response) {
  const auto policy = response.headers.find("referrer-policy");
  contexts_.received(id, response.url, policy == response.headers.end() ? "" : policy->second);
  if (id != response.url) {
    const auto* environment = contexts_.find(id);
    if (environment && environment->module)
      contexts_.remember(response.url, response.url, {"", environment->request.policy},
                         environment->module);
  }
  contexts_.dispatched(id);
}
ModuleLoader::ModuleLoader(JSRuntime* runtime, std::string base_url, ExecutionBudget* execution,
                           ScriptLoading* loading, const std::string* referrer_policy,
                           std::shared_ptr<std::atomic<bool>> cancelled)
    : base_url_(std::move(base_url)),
      referrer_policy_(referrer_policy),
      loading_(loading),
      execution_(execution),
      cancelled_(std::move(cancelled)) {
  JS_SetModuleLoaderFunc(runtime, normalize, load, this);
}

char* ModuleLoader::normalize(JSContext* context, const char* base, const char* name,
                              void* opaque) {
  try {
    (void)opaque;
    auto& loader = *static_cast<ScriptContext*>(JS_GetContextOpaque(context))->modules;
    const auto* parent = base ? loader.contexts_.find(base) : nullptr;
    const std::string importing_url =
        parent ? parent->base_url
        : base && *base && (std::string_view(base).find(":") != std::string_view::npos)
            ? base
            : loader.base_url_;
    const auto url = loader.import_map_.resolve(name, importing_url);
    loader.contexts_.remember(
        url, url, {importing_url, parent ? parent->request.policy : *loader.referrer_policy_});
    auto* result = static_cast<char*>(js_malloc(context, url.size() + 1));
    if (result) std::memcpy(result, url.c_str(), url.size() + 1);
    return result;
  } catch (const CliError& error) {
    JS_ThrowInternalError(context, "%s: %s", error.code.c_str(), error.what());
    return nullptr;
  } catch (const std::exception& error) {
    JS_ThrowInternalError(context, "%s", error.what());
    return nullptr;
  }
}

bool ModuleLoader::import_meta(JSContext* context, JSModuleDef* module, const std::string& url) {
  auto meta = JS_GetImportMeta(context, module);
  if (JS_IsException(meta)) return false;
  const auto* environment = contexts_.find(url);
  const auto& final_url = environment ? environment->base_url : url;
  const int status =
      JS_SetPropertyStr(context, meta, "url", JS_NewString(context, final_url.c_str()));
  JS_FreeValue(context, meta);
  return status >= 0;
}

JSModuleDef* ModuleLoader::load(JSContext* context, const char* name, void* opaque) {
  try {
    (void)opaque;
    auto& loader = *static_cast<ScriptContext*>(JS_GetContextOpaque(context))->modules;
    LoadTrace trace("module");
    const auto cached = loader.compiled_.find(name);
    if (cached != loader.compiled_.end())
      return static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(cached->second));
    std::string source;
    {
      // Pause CPU accounting during network waiting, including failed transfers.
      // The shared wall-clock loading deadline continues to advance.
      struct NetworkWait {
        ModuleLoader& loader;
        std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
        ~NetworkWait() {
          const auto elapsed = std::chrono::steady_clock::now() - started;
          loader.execution_->pause_for(elapsed);
        }
      } wait{loader};
      if (std::string_view(name).rfind("blob:", 0) == 0) {
        auto& state = *static_cast<ScriptContext*>(JS_GetContextOpaque(context));
        const auto found = state.network.blob_urls.find(name);
        if (found == state.network.blob_urls.end()) {
          JS_ThrowReferenceError(context, "blob module URL is unavailable or revoked");
          return nullptr;
        }
        if (found->second.size() > kMaxScriptBytes) {
          JS_ThrowInternalError(context, "blob module source quota exceeded");
          return nullptr;
        }
        source = found->second;
      } else
        source = loader.source(name);
    }
    trace.mark("source");
    loader.preload_dependencies(source, name);
    trace.mark("dependencies");
    auto compiled = JS_Eval(context, source.data(), source.size(), name,
                            JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    trace.mark("compile");
    if (JS_IsException(compiled)) return nullptr;
    auto* module = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(compiled));
    const bool ok = loader.import_meta(context, module, name);
    if (ok) {
      loader.compiled_.emplace(name, JS_DupValue(context, compiled));
      loader.release_source(name);
    }
    JS_FreeValue(context, compiled);
    return ok ? module : nullptr;
  } catch (const CliError& error) {
    JS_ThrowInternalError(context, "%s: %s", error.code.c_str(), error.what());
    return nullptr;
  } catch (const std::exception& error) {
    JS_ThrowInternalError(context, "%s", error.what());
    return nullptr;
  }
}

JSValue ModuleLoader::evaluate(JSContext* context, const std::string& source,
                               const std::string& url) {
  if (source.size() > kMaxScriptBytes)
    return JS_ThrowInternalError(context, "module source quota exceeded");
  const auto cached = compiled_.find(url);
  if (cached != compiled_.end())
    return JS_EvalFunction(context, JS_DupValue(context, cached->second));
  preload_dependencies(source, url);
  auto compiled = JS_Eval(context, source.data(), source.size(), url.c_str(),
                          JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
  if (JS_IsException(compiled)) return compiled;
  if (!import_meta(context, static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(compiled)), url)) {
    JS_FreeValue(context, compiled);
    return JS_EXCEPTION;
  }
  compiled_.emplace(url, JS_DupValue(context, compiled));
  release_source(url);
  return JS_EvalFunction(context, compiled);
}

void ModuleLoader::add_import_map(JSContext* context, const std::string& source) {
  import_map_.add(context, source, base_url_);
}
}  // namespace webcli
