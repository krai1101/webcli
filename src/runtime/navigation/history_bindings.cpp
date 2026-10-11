#include "runtime/navigation/history_bindings.hpp"

#include <stdexcept>

#include "network/http_client.hpp"
#include "runtime/engine/js_handles.hpp"
#include "runtime/engine/script_context.hpp"

namespace webcli {
namespace {
constexpr std::size_t kPendingHistoryActions = 256;
bool has_capacity(JSContext* context, const ScriptContext& state) {
  if (state.history_actions.size() < kPendingHistoryActions) return true;
  JS_ThrowRangeError(context, "history update queue exceeds 256 actions");
  return false;
}
}  // namespace
JSValue js_history_commit(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  if (argc < 4)
    return JS_ThrowTypeError(context, "URL, state, replace flag and previous ID required");
  auto& state = *static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  auto url = js_string(context, argv[0]);
  std::int32_t previous = 0;
  if (JS_ToInt32(context, &previous, argv[3]) < 0) return JS_EXCEPTION;
  const int replace = JS_ToBool(context, argv[2]);
  if (replace < 0) return JS_EXCEPTION;
  if (url.size() > 8192) return JS_ThrowRangeError(context, "history URL exceeds 8 KiB");
  if (!same_origin(url, state.history_source_url))
    return JS_ThrowTypeError(context, "invalid same-origin history update");
  std::size_t size = 0;
  const auto free_buffer = [context](std::uint8_t* bytes) { js_free(context, bytes); };
  const std::unique_ptr<std::uint8_t, decltype(free_buffer)> serialized(
      JS_WriteObject(context, &size, argv[1], JS_WRITE_OBJ_REFERENCE), free_buffer);
  if (!serialized) return JS_EXCEPTION;
  if (size > HistoryArchive::kMaxBytes)
    return JS_ThrowRangeError(context, "serialized state exceeds 8 MiB");
  // Serialization can invoke getters. Inspect the queue only after it finishes.
  auto* tail = state.history_actions.empty() ? nullptr : &state.history_actions.back();
  const bool coalesce = replace && tail && tail->kind != HistoryAction::Kind::Traverse &&
                        tail->state && tail->state->id == previous;
  if (!coalesce && !has_capacity(context, state)) return JS_EXCEPTION;
  // Consecutive replacements have no observable intermediate native entry.
  // Reuse that notification and credit only a payload with one remaining owner.
  const auto credit = coalesce && tail->state.use_count() == 1 ? tail->state->bytes.size() : 0;
  HistoryStatePtr snapshot;
  try {
    snapshot = state.history_archive->store(
        std::vector<std::uint8_t>(serialized.get(), serialized.get() + size), url, credit);
  } catch (const std::length_error& error) {
    return JS_ThrowRangeError(context, "%s", error.what());
  }
  const auto id = snapshot->id;
  const auto kind = replace ? HistoryAction::Kind::Replace : HistoryAction::Kind::Push;
  if (coalesce) {
    tail->url = std::move(url);
    tail->state = std::move(snapshot);
  } else
    state.history_actions.push_back({kind, std::move(url), std::move(snapshot), 0});
  return JS_NewInt32(context, id);
}
JSValue js_history_traverse(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  if (!argc) return JS_ThrowTypeError(context, "history delta required");
  auto& state = *static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  if (!has_capacity(context, state)) return JS_EXCEPTION;
  std::int32_t delta = 0;
  if (JS_ToInt32(context, &delta, argv[0]) < 0) return JS_EXCEPTION;
  state.history_actions.push_back({HistoryAction::Kind::Traverse, {}, nullptr, delta});
  return JS_UNDEFINED;
}
JSValue js_history_read(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  if (!argc) return JS_ThrowTypeError(context, "state ID required");
  std::int32_t id = 0;
  if (JS_ToInt32(context, &id, argv[0]) < 0) return JS_EXCEPTION;
  if (!id) return JS_NULL;
  auto& state = *static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  const auto snapshot = state.history_archive->find(id);
  if (!snapshot) return JS_ThrowInternalError(context, "history snapshot no longer retained");
  // The archive spans documents and origins. A guessed private snapshot ID
  // must never let another origin deserialize an earlier document's state.
  if (!same_origin(snapshot->source_url, state.history_source_url))
    return JS_ThrowTypeError(context, "history snapshot belongs to another origin");
  return JS_ReadObject(context, snapshot->bytes.data(), snapshot->bytes.size(),
                       JS_READ_OBJ_REFERENCE);
}
}  // namespace webcli
