#include "runtime/scripts/document_executor.hpp"

#include <algorithm>
#include <chrono>
#include <thread>

#include "core/load_trace.hpp"
#include "core/script_types.hpp"
#include "core/serialization/json_string.hpp"
#include "network/http_client.hpp"
#include "runtime/dom/dom_bindings.hpp"
#include "runtime/dom/dom_helpers.hpp"
#include "runtime/engine/diagnostics.hpp"
#include "runtime/engine/js_handles.hpp"
#include "runtime/engine/js_runtime.hpp"
#include "runtime/engine/script_context.hpp"
#include "runtime/page/lifecycle_delivery.hpp"
#include "runtime/scripts/document_scripts.hpp"
#include "runtime/scripts/parser_preloads.hpp"
namespace webcli {
void execute_document_scripts(JsRuntime& runtime, ScriptContext& s, Page& page,
                              std::vector<DocumentScript>& scripts, bool finish) {
  LoadTrace trace("scripts");
  const auto order = order_document_scripts(scripts);
  for (const auto& script : scripts)
    runtime.evaluate(
        "__wrap(" + std::to_string(dom_node_id(&s, script.node)) + ")._parserPending=true;",
        "parser script preparation");
  // Fetch ahead through the shared bounded resource queue. Classic scripts still
  // execute in parser order; this does not run speculative JavaScript.
  for (const auto& script : scripts)
    if (!script.url.empty() && !script.import_map &&
        (!script.module || is_async_document_script(script)))
      runtime.evaluate("__preloadParserScript(__wrap(" +
                           std::to_string(dom_node_id(&s, script.node)) + ")," +
                           json_escape(script.url) + ");",
                       "parser preload scanner");
  bool parsing_complete = false;
  const auto finish_parsing = [&] {
    if (parsing_complete) return;
    parsing_complete = true;
    if (s.lifecycle.finish_parser())
      runtime.evaluate("document.dispatchEvent(new Event('readystatechange'));",
                       "document parsing complete");
  };
  const auto wait_for_scripts = [&](bool async_scripts) {
    while (true) {
      if (s.execution.cancelled()) throw CliError("REQUEST_CANCELLED", "script loading cancelled");
      bool pending = std::any_of(s.pending_modules.begin(), s.pending_modules.end(),
                                 [async_scripts](const PendingModule& module) {
                                   return async_scripts || module.blocks_content_loaded;
                                 });
      if (async_scripts) {
        JsValueOwner scope(s.context, JS_GetGlobalObject(s.context));
        JsValueOwner count(s.context,
                           JS_GetPropertyStr(s.context, scope.get(), "__webcliPendingScriptLoads"));
        int32_t number = 0;
        if (JS_ToInt32(s.context, &number, count.get()) < 0) {
          record_async_js_error(&s, s.context);
          break;
        }
        pending = pending || number > 0;
      }
      if (!pending) break;
      try {
        s.script_loading.request_timeout();
      } catch (const CliError& error) {
        if (page.script_errors.size() < kMaxScriptErrors)
          page.script_errors.push_back(error.code + ": " + error.what());
        break;
      }
      runtime.pump();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  };
  for (std::size_t position = 0; position < order.indices.size(); ++position) {
    if (finish && position == order.parsing_end) finish_parsing();
    if (!begin_js_task(&s)) {
      s.lifecycle.abort();
      break;
    }
    const auto i = order.indices[position];
    auto& script = scripts[i];
    LoadTrace script_trace("parser-script");
    if (!script.failure.empty()) {
      if (page.script_errors.size() < kMaxScriptErrors)
        page.script_errors.push_back(script.failure);
      continue;
    }
    if (script.import_map) {
      try {
        if (!script.url.empty())
          throw CliError("IMPORT_MAP", "external import maps are unsupported");
        s.modules->add_import_map(s.context, script.source);
      } catch (const CliError& error) {
        if (page.script_errors.size() < kMaxScriptErrors)
          page.script_errors.push_back(error.code + ": " + error.what());
      }
      continue;
    }
    const auto script_id = dom_node_id(&s, script.node);
    const auto target = "__wrap(" + std::to_string(script_id) + ")";
    if (is_async_document_script(script) && !script.url.empty()) {
      runtime.evaluate("__startParserAsync(" + target + ");", "async parser script initialization");
      runtime.checkpoint();
      continue;
    }
    if (!runtime.evaluate(target + "._parserPending=false;" + target + "._executed=true;",
                          "script initialization"))
      continue;
    if (!script.url.empty()) {
      std::optional<std::string> source;
      std::string failure;
      try {
        if (!script.module) {
          source = await_parser_source(runtime, s, script_id);
          script_trace.mark("download");
        } else {
          source = s.modules->source(script.url);
        }
      } catch (const CliError& error) {
        failure = error.code + ": " + error.what();
      } catch (const std::exception& error) {
        failure = error.what();
      }
      if (!failure.empty()) {
        if (page.script_errors.size() < kMaxScriptErrors)
          page.script_errors.push_back("could not load bounded " +
                                       std::string(script.module ? "module" : "classic script") +
                                       ": " + script.url + ": " + failure);
        runtime.evaluate(target + ".dispatchEvent(new Event('error'));", "script resource failure");
        continue;
      }
      script.source = std::move(*source);
    }
    const auto origin = script.url.empty()
                            ? (script.module ? page.url + "#inline-module-" : "inline script ") +
                                  std::to_string(script_id)
                        : script.module ? script.url
                                        : classic_script_id(script.url, script_id);
    s.modules->remember_script(origin, script.url.empty() ? script.base_url : script.url,
                               {"", script.referrer_policy}, script.module);
    if (script.module) {
      s.error_context = origin;
      if (!begin_js_task(&s)) {
        s.lifecycle.abort();
        break;
      }
      auto result = s.modules->evaluate(s.context, script.source, origin);
      if (JS_IsException(result)) record_async_js_error(&s, s.context);
      // Module evaluation returns a promise, including synchronous module errors.
      if (!JS_IsException(result) && JS_PromiseState(s.context, result) == JS_PROMISE_PENDING)
        s.pending_modules.push_back({result, origin, !is_async_document_script(script)});
      else
        JS_FreeValue(s.context, result);
    } else {
      JsValueOwner scope(s.context, JS_GetGlobalObject(s.context));
      JsValueOwner previous_script(
          s.context, JS_GetPropertyStr(s.context, scope.get(), "__webcliCurrentScript"));
      JS_SetPropertyStr(s.context, scope.get(), "__webcliCurrentScript",
                        JS_NewInt32(s.context, dom_node_id(&s, script.node)));
      const auto success = runtime.evaluate(script.source, origin);
      JS_SetPropertyStr(s.context, scope.get(), "__webcliCurrentScript", previous_script.release());
      if (!script.url.empty())
        runtime.evaluate(
            target + ".dispatchEvent(new Event('" + (success ? "load" : "error") + "'));",
            origin + " resource event");
    }
    script_trace.mark("execute");
    runtime.checkpoint();
    script_trace.mark("checkpoint");
  }
  trace.mark("scripts");
  if (!finish) return;
  finish_parsing();
  s.lifecycle.finish_script_processing();
  wait_for_scripts(false);
  advance_document_lifecycle(runtime, s);
  runtime.pump();
  wait_for_scripts(true);
  advance_document_lifecycle(runtime, s);
  s.modules->finish_initial_loading();
  runtime.pump();
  trace.mark("load-events");
  runtime.sync(page);
}
}  // namespace webcli
