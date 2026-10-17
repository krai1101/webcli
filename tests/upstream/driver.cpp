// Test-only adapter for the unmodified Lightpanda demo PandaScripts.
// This entry point is compiled only with WEBCLI_UPSTREAM_TEST_DRIVER=ON.
#include <curl/curl.h>
#include <quickjs.h>

#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <thread>

#include "network/cookie_store.hpp"
#include "network/http_client.hpp"
#include "network/http_handles.hpp"
#include "network/resource_service.hpp"
#include "runtime/engine/js_handles.hpp"
#include "runtime/page/browsing_instance.hpp"

namespace {
using namespace webcli;
using Clock = std::chrono::steady_clock;
struct Transport {
  Transport() { initialize_http_transport(); }
  ~Transport() {
    shutdown_resource_service();
    shutdown_http_handles();
    shutdown_cookie_store();
    curl_global_cleanup();
  }
};
struct Driver {
  BrowsingInstance browser;
};
JSValue call(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  try {
    if (argc < 2) throw std::runtime_error("adapter requires operation and argument");
    auto& driver = *static_cast<Driver*>(JS_GetContextOpaque(context));
    auto& browser = driver.browser;
    const auto operation = js_string(context, argv[0]);
    const auto argument = js_string(context, argv[1]);
    if (operation == "evaluate") {
      const auto json = browser.test_evaluate(argument);
      return JS_ParseJSON(context, json.data(), json.size(), "page-evaluation-result");
    }
    EngineRequest request;
    request.target = argument;
    // Lightpanda tools.zig defaults waits on a loaded document to 5000 ms.
    request.timeout_ms = 5000;
    if (operation == "goto")
      request.action = BrowserAction::Open;
    else if (operation == "waitForSelector" || operation == "waitForState") {
      request.action = BrowserAction::Wait;
      request.target.clear();
      request.condition =
          operation == "waitForSelector" ? WaitCondition::Selector : WaitCondition::Idle;
      if (operation == "waitForSelector") request.value = argument;
      if (operation == "waitForState" && argument != "networkidle")
        throw std::runtime_error("unsupported upstream wait state: " + argument);
    } else if (operation == "waitForScript") {
      const auto deadline = Clock::now() + std::chrono::milliseconds(request.timeout_ms);
      for (;;) {
        browser.tick();
        if (browser.test_evaluate("Boolean((" + argument + "))") == "true") return JS_UNDEFINED;
        if (Clock::now() >= deadline)
          throw std::runtime_error("waitForScript timed out: " + argument);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    } else {
      if (operation == "click")
        request.action = BrowserAction::Click;
      else if (operation == "fill" || operation == "setChecked")
        request.action = BrowserAction::Fill;
      else if (operation == "selectOption")
        request.action = BrowserAction::Select;
      else
        throw std::runtime_error("unsupported upstream action: " + operation);
      if (argc > 2) request.value = js_string(context, argv[2]);
      browser.test_action(request);
      return JS_UNDEFINED;
    }
    browser.execute(request);
    return JS_UNDEFINED;
  } catch (const std::exception& error) {
    return JS_ThrowInternalError(context, "%s", error.what());
  }
}
std::string read(const char* path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) throw std::runtime_error(std::string("cannot read ") + path);
  return {std::istreambuf_iterator<char>(file), {}};
}
void check(JSContext* context, JSValueConst value) {
  if (!JS_IsException(value)) return;
  const JsValueOwner error(context, JS_GetException(context));
  throw std::runtime_error(js_string(context, error.get()));
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 3) throw std::runtime_error("usage: upstream-driver adapter.js script.js");
    Transport transport;
    Driver driver;
    const auto runtime =
        std::unique_ptr<JSRuntime, decltype(&JS_FreeRuntime)>(JS_NewRuntime(), JS_FreeRuntime);
    if (!runtime) throw std::bad_alloc();
    const auto context = std::unique_ptr<JSContext, decltype(&JS_FreeContext)>(
        JS_NewContext(runtime.get()), JS_FreeContext);
    if (!context) throw std::bad_alloc();
    JS_SetContextOpaque(context.get(), &driver);
    const JsValueOwner global(context.get(), JS_GetGlobalObject(context.get()));
    JS_SetPropertyStr(context.get(), global.get(), "__call",
                      JS_NewCFunction(context.get(), call, "__call", 3));
    const auto source = read(argv[1]) + "\nglobalThis.__result = (async function(){\n" +
                        read(argv[2]) +
                        "\n})().then(value=>({ok:true,value}),error=>({ok:false,error:String(error)"
                        ",stack:error.stack}));";
    const JsValueOwner result(context.get(), JS_Eval(context.get(), source.data(), source.size(),
                                                     "upstream-script", JS_EVAL_TYPE_GLOBAL));
    check(context.get(), result.get());
    const JsValueOwner promise(context.get(),
                               JS_GetPropertyStr(context.get(), global.get(), "__result"));
    const auto deadline = Clock::now() + std::chrono::seconds(120);
    while (JS_PromiseState(context.get(), promise.get()) == JS_PROMISE_PENDING) {
      JSContext* job = nullptr;
      if (JS_ExecutePendingJob(runtime.get(), &job) < 0) check(job, JS_EXCEPTION);
      if (Clock::now() >= deadline)
        throw std::runtime_error("upstream script exceeded harness deadline");
    }
    const JsValueOwner output(context.get(), JS_PromiseResult(context.get(), promise.get()));
    const JsValueOwner json(
        context.get(), JS_JSONStringify(context.get(), output.get(), JS_UNDEFINED, JS_UNDEFINED));
    check(context.get(), json.get());
    std::cout << js_string(context.get(), json.get()) << '\n';
    const JsValueOwner ok(context.get(), JS_GetPropertyStr(context.get(), output.get(), "ok"));
    return JS_ToBool(context.get(), ok.get()) == 1 ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
