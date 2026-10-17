#pragma once
#include <quickjs.h>

#include <atomic>
#include <future>
#include <memory>
#include <optional>
#include <string>

#include "network/http_client.hpp"
#include "runtime/page/document_lifecycle.hpp"
namespace webcli {
// Promise handles belong to the realm thread; transport owns only byte results.
struct FetchJob {
  std::unique_ptr<DocumentLifecycle::LoadBlocker> load_blocker;
  std::string script_url;
  std::string diagnostic_request;
  int id = 0;
  std::optional<HttpRequest> queued_request;
  bool script = false;
  bool opaque = false;
  JSContext* context = nullptr;
  std::future<HttpResponse> future;
  std::shared_ptr<std::atomic<bool>> cancelled;
  JSValue resolve = JS_UNDEFINED, reject = JS_UNDEFINED;
  ~FetchJob() {
    if (cancelled) cancelled->store(true);
    if (context) {
      JS_FreeValue(context, resolve);
      JS_FreeValue(context, reject);
    }
  }
};
}  // namespace webcli
