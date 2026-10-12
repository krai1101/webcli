#include "runtime/scripts/module_requests.hpp"

#include <quickjs.h>

#include <cstring>
#include <exception>
#include <memory>

#include "core/errors.hpp"
#include "core/resource_limits.hpp"
#include "core/runtime_policy.hpp"
#include "runtime/engine/execution_budget.hpp"
#include "runtime/engine/js_handles.hpp"

namespace webcli {
namespace {
struct Requests {
  std::vector<std::string> names;
  std::exception_ptr failure;
};
int empty_module(JSContext*, JSModuleDef*) { return 0; }
char* identity(JSContext* context, const char*, const char* name, void*) {
  // Preserve the parser's decoded specifier. The real loader owns URL/import-map
  // resolution; discovery must not reinterpret JavaScript strings or paths.
  const auto size = std::strlen(name) + 1;
  auto* copy = static_cast<char*>(js_malloc(context, size));
  if (copy) std::memcpy(copy, name, size);
  return copy;
}
JSModuleDef* collect(JSContext* context, const char* name, void* opaque) noexcept {
  auto& requests = *static_cast<Requests*>(opaque);
  try {
    requests.names.emplace_back(name);
    return JS_NewCModule(context, name, empty_module);
  } catch (...) {
    requests.failure = std::current_exception();
    JS_ThrowOutOfMemory(context);
    return nullptr;
  }
}
int interrupt(JSRuntime*, void* opaque) {
  return static_cast<ExecutionBudget*>(opaque)->interrupt();
}
}  // namespace

std::vector<std::string> module_requests(const std::string& source, const std::string& origin,
                                         ExecutionBudget& budget) {
  // QuickJS's public API resolves requests during compilation and does not
  // expose V8's GetModuleRequests. A short-lived compilation realm supplies
  // inert dependency placeholders, so its parser can enumerate requests without
  // blocking on their downloads. Nothing is instantiated or evaluated here.
  // Destroy the realm after each source: no duplicate bytecode is retained.
  const std::unique_ptr<JSRuntime, decltype(&JS_FreeRuntime)> runtime(JS_NewRuntime(),
                                                                      JS_FreeRuntime);
  if (!runtime) throw CliError("RUNTIME_INIT", "cannot allocate module discovery runtime");
  if (runtime_policy().js_heap_bytes)
    JS_SetMemoryLimit(runtime.get(), runtime_policy().js_heap_bytes);
  JS_SetMaxStackSize(runtime.get(), kJsStackBytes);
  JS_SetInterruptHandler(runtime.get(), interrupt, &budget);
  Requests requests;
  JS_SetModuleLoaderFunc(runtime.get(), identity, collect, &requests);
  const std::unique_ptr<JSContext, decltype(&JS_FreeContext)> context(JS_NewContext(runtime.get()),
                                                                      JS_FreeContext);
  if (!context) throw CliError("RUNTIME_INIT", "cannot allocate module discovery context");
  const JsValueOwner parsed(context.get(),
                            JS_Eval(context.get(), source.data(), source.size(), origin.c_str(),
                                    JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY));
  if (requests.failure) std::rethrow_exception(requests.failure);
  if (budget.cancelled()) throw CliError("REQUEST_CANCELLED", "module discovery cancelled");
  // Syntax/import-attribute failures are diagnosed by the actual compilation in
  // the document's context, rather than creating a second unrelated exception.
  if (JS_IsException(parsed.get())) {
    const JsValueOwner exception(context.get(), JS_GetException(context.get()));
    return {};
  }
  return std::move(requests.names);
}
}  // namespace webcli
