
#include "core/url/url.hpp"
#include "runtime/dom/dom_bindings.hpp"
#include "runtime/engine/diagnostics.hpp"
#include "runtime/engine/js_handles.hpp"
#include "runtime/engine/script_context.hpp"
#include "runtime/frames/frame_tree.hpp"
#include "runtime/messaging/message_ports.hpp"

namespace webcli {
namespace {
void release_realm(JSRuntime*, JSValue value) {
  delete static_cast<std::shared_ptr<ScriptContext>*>(JS_GetOpaque(value, JS_GetClassID(value)));
}
bool accessible(const ScriptContext& from, const ScriptContext& to) {
  return &from == &to ||
         (!from.opaque_origin && !to.opaque_origin && from.security_origin == to.security_origin);
}
JSValue result_in(JSContext* context, JSContext* origin, JSValue result) {
  return JS_IsException(result) ? JS_Throw(context, JS_GetException(origin)) : result;
}
}  // namespace
void retain_frame_realm(ScriptContext& state, std::shared_ptr<ScriptContext> owner) {
  if (state.frame_id)
    state.frame_owner_class = state.frame_tree->state(0)->frame_owner_class;
  else
    JS_NewClassID(state.runtime, &state.frame_owner_class);
  const auto realm_owner_class = state.frame_owner_class;
  if (!JS_IsRegisteredClass(state.runtime, realm_owner_class)) {
    JSClassDef definition{};
    definition.class_name = "WebCLIRealmOwner";
    definition.finalizer = release_realm;
    if (JS_NewClass(state.runtime, realm_owner_class, &definition) < 0) throw std::bad_alloc();
  }
  const auto object = JS_NewObjectClass(state.context, realm_owner_class);
  if (JS_IsException(object)) throw std::bad_alloc();
  try {
    JS_SetOpaque(object, new std::shared_ptr<ScriptContext>(std::move(owner)));
  } catch (...) {
    JS_FreeValue(state.context, object);
    throw;
  }
  // Context-owned class prototypes cannot be deleted by page code. A retained
  // function/document keeps its realm and native DOM alive after frame removal.
  JS_SetClassProto(state.context, realm_owner_class, object);
}
JSValue frame_security_error(JSContext* context, const char* message) {
  return JS_ThrowDOMException(context, "SecurityError", "%s", message);
}
JSValue js_frame_access(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  auto& caller = *static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  auto* tree = caller.frame_tree;
  if (!tree || !argc) return JS_NULL;
  const auto operation = js_string(context, argv[0]);
  if (operation == "self") return JS_NewInt32(context, caller.frame_id);
  if (operation == "open") {
    if (argc < 4) return JS_ThrowTypeError(context, "popup URL and name required");
    const auto raw = js_string(context, argv[1]);
    const auto resolved = raw.empty() || raw == "about:blank"
                              ? std::optional<std::string>(raw)
                              : resolve_web_url(caller.base_url, raw);
    if (!resolved) return JS_ThrowDOMException(context, "SyntaxError", "invalid popup URL");
    return JS_NewInt32(context, tree->open_popup(caller, *resolved, js_string(context, argv[2]),
                                                 JS_ToBool(context, argv[3]) > 0,
                                                 argc > 4 && JS_ToBool(context, argv[4]) > 0));
  }
  if (operation == "element-id") {
    auto* node = argc > 1 ? dom_node_arg(context, argv[1]) : nullptr;
    return JS_NewInt32(context, tree->ensure(caller, node));
  }
  int32_t id = caller.frame_id;
  if (argc > 1 && JS_ToInt32(context, &id, argv[1]) < 0) return JS_EXCEPTION;
  auto* target = tree->state(id);
  if (operation == "closed") return JS_NewBool(context, !target);
  if (operation == "popup-window") {
    auto* owner = tree->state(0);
    if (!owner || !accessible(caller, *owner)) owner = &caller;
    const JsValueOwner global(owner->context, JS_GetGlobalObject(owner->context));
    const JsValueOwner proxy(owner->context, JS_GetPropertyStr(owner->context, global.get(), "__webcliWindowProxy"));
    JSValue argument = JS_NewInt32(owner->context, id);
    return result_in(context, owner->context, JS_Call(owner->context, proxy.get(), JS_UNDEFINED, 1, &argument));
  }
  if (operation == "close") {
    tree->close_popup(id);
    return JS_UNDEFINED;
  }
  if (!target) return JS_NULL;
  if (operation == "parent") return JS_NewInt32(context, tree->parent(id));
  if (operation == "top") return JS_NewInt32(context, tree->top(id));
  if (operation == "opener") return JS_NewInt32(context, tree->opener(id));
  if (operation == "length") {
    tree->scan(*target);
    return JS_NewInt32(context, tree->count(id));
  }
  if (operation == "child") {
    int32_t index = 0;
    if (argc > 2 && JS_ToInt32(context, &index, argv[2]) < 0) return JS_EXCEPTION;
    tree->scan(*target);
    return JS_NewInt32(context, tree->child(id, index));
  }
  if (operation == "document" && !accessible(caller, *target)) return JS_NULL;
  if (operation == "frame-element") {
    auto* parent = tree->state(tree->parent(id));
    auto* node = tree->element(id);
    if (!node || !parent || !accessible(caller, *parent)) return JS_NULL;
    const JsValueOwner global(parent->context, JS_GetGlobalObject(parent->context));
    const JsValueOwner wrap(parent->context,
                            JS_GetPropertyStr(parent->context, global.get(), "__wrap"));
    JSValue argument = JS_NewInt32(context, dom_node_id(parent, node));
    return result_in(context, parent->context,
                     JS_Call(parent->context, wrap.get(), JS_UNDEFINED, 1, &argument));
  }
  if (operation == "message") {
    if (argc < 4) return JS_ThrowTypeError(context, "message and target origin required");
    auto required = js_string(context, argv[3]);
    if (required == "/")
      required = caller.security_origin;
    else if (required != "*")
      required = url_origin(required);
    std::size_t size = 0;
    auto* bytes = JS_WriteObject(context, &size, argv[2], JS_WRITE_OBJ_REFERENCE);
    if (!bytes) {
      JS_FreeValue(context, JS_GetException(context));
      return JS_ThrowDOMException(context, "DataCloneError", "message cannot be cloned");
    }
    auto cloned = JS_ReadObject(target->context, bytes, size, JS_READ_OBJ_REFERENCE);
    js_free(context, bytes);
    if (JS_IsException(cloned)) {
      JS_FreeValue(target->context, JS_GetException(target->context));
      return JS_ThrowDOMException(context, "DataCloneError", "message cannot be cloned");
    }
    auto data = std::make_shared<JsValueOwner>(target->context, cloned);
    std::vector<int> transfer;
    if (argc > 4) {
      const JsValueOwner length(context, JS_GetPropertyStr(context, argv[4], "length"));
      uint32_t count;
      if (JS_ToUint32(context, &count, length.get()) < 0) return JS_EXCEPTION;
      for (uint32_t index = 0; index < count; ++index) {
        const JsValueOwner item(context, JS_GetPropertyUint32(context, argv[4], index));
        int32_t port;
        if (JS_ToInt32(context, &port, item.get()) < 0) return JS_EXCEPTION;
        transfer.push_back(port);
      }
    }
    auto adopted = std::make_shared<std::vector<int>>();
    if (!transfer.empty()) {
      if (!caller.ports)
        return JS_ThrowDOMException(context, "DataCloneError", "invalid transferred port");
      if (!target->ports) target->ports = std::make_shared<MessagePorts>(*target);
    }
    const auto origin = caller.opaque_origin ? "null" : caller.security_origin;
    const int source = caller.frame_id;
    const auto task = target->scheduler.schedule(
        TaskScheduler::Source::Resource, {},
        [tree, id, source, origin, required, data, target, adopted] {
          // A queued message must not enter a replacement document or a removed frame.
          if (tree->state(id) != target || !target->active ||
              (required != "*" && (target->opaque_origin || target->security_origin != required))) {
            if (target->ports)
              for (int port : *adopted) target->ports->close(port);
            return;
          }
          const JsValueOwner ports(target->context, JS_NewArray(target->context));
          for (uint32_t index = 0; index < adopted->size(); ++index)
            JS_SetPropertyUint32(target->context, ports.get(), index,
                                 JS_NewInt32(target->context, (*adopted)[index]));
          JSValue args[] = {data->get(), JS_NewString(target->context, origin.c_str()),
                            JS_NewInt32(target->context, source), ports.get()};
          auto result =
              JS_Call(target->context, target->frame_message_delivery, JS_UNDEFINED, 4, args);
          JS_FreeValue(target->context, args[1]);
          if (JS_IsException(result)) record_async_js_error(target, target->context);
          JS_FreeValue(target->context, result);
        });
    // A failed queue allocation or invalid transfer must leave sender ports
    // intact. The task cannot execute until this synchronous binding returns.
    try {
      if (!transfer.empty()) *adopted = caller.ports->transfer_to(*target->ports, transfer);
    } catch (const CliError& error) {
      target->scheduler.cancel(task);
      return JS_ThrowDOMException(context, "DataCloneError", "%s", error.what());
    } catch (...) {
      target->scheduler.cancel(task);
      throw;
    }
    return JS_UNDEFINED;
  }
  if (!accessible(caller, *target))
    return frame_security_error(context, "cross-origin frame access denied");
  const JsValueOwner global(target->context, JS_GetGlobalObject(target->context));
  if (operation == "document")
    return result_in(context, target->context,
                     JS_GetPropertyStr(target->context, global.get(), "document"));
  if (operation == "navigate") {
    if (!id) return JS_ThrowTypeError(context, "frame proxy cannot navigate the top-level page");
    auto url =
        argc > 2 ? resolve_web_url(target->base_url, js_string(context, argv[2])) : std::nullopt;
    if (!url) return JS_ThrowTypeError(context, "invalid frame URL");
    NavigationAction action;
    action.url = *url;
    action.referrer = {caller.base_url, caller.referrer_policy};
    target->navigation = std::move(action);
    return JS_UNDEFINED;
  }
  if (argc < 3) return JS_UNDEFINED;
  const JSAtom key = JS_ValueToAtom(context, argv[2]);
  if (key == JS_ATOM_NULL) return JS_EXCEPTION;
  JSValue result = JS_UNDEFINED;
  if (operation == "get")
    result = JS_GetProperty(target->context, global.get(), key);
  else if (operation == "set" && argc > 3) {
    const int status =
        JS_SetProperty(target->context, global.get(), key, JS_DupValue(context, argv[3]));
    result = status < 0 ? JS_EXCEPTION : JS_TRUE;
  }
  JS_FreeAtom(context, key);
  return result_in(context, target->context, result);
}
}  // namespace webcli
