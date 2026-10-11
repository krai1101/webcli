#include "runtime/intl/segmenter_bindings.hpp"

#include <unicode/ubrk.h>
#include <unicode/uloc.h>

#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "core/errors.hpp"
#include "runtime/engine/js_handles.hpp"
#include "runtime/engine/script_context.hpp"

namespace webcli {
namespace {
using BreakIterator = std::unique_ptr<UBreakIterator, decltype(&ubrk_close)>;
struct SegmentCursor {
  // ICU clones retain pointers into this text. Share the allocation across
  // independent iterators; it must outlive every native break iterator.
  std::shared_ptr<std::vector<UChar>> text;
  BreakIterator iterator{nullptr, ubrk_close};
  bool word = false;
};
void finalize_cursor(JSRuntime*, JSValue value) {
  delete static_cast<SegmentCursor*>(JS_GetOpaque(value, JS_GetClassID(value)));
}
JSValue cursor_object(JSContext* context, std::unique_ptr<SegmentCursor> cursor) {
  const auto* state = static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  JSValue object = JS_NewObjectClass(context, state->segmenter_class);
  if (JS_IsException(object)) return object;
  JS_SetOpaque(object, cursor.release());
  return object;
}
JSValue icu_error(JSContext* context, UErrorCode error) {
  return JS_ThrowInternalError(context, "Unicode segmentation failed: %s", u_errorName(error));
}
bool locale_id(const std::string& tag, std::string& id) {
  // BCP47 tags are ASCII. Embedded NUL must not silently truncate a locale.
  if (tag.empty() || tag.find('\0') != std::string::npos ||
      tag.size() >
          static_cast<size_t>(std::numeric_limits<int32_t>::max() - ULOC_FULLNAME_CAPACITY))
    return false;
  UErrorCode error = U_ZERO_ERROR;
  int32_t parsed = 0;
  std::vector<char> buffer(tag.size() + ULOC_FULLNAME_CAPACITY);
  const auto length = uloc_forLanguageTag(tag.c_str(), buffer.data(),
                                          static_cast<int32_t>(buffer.size()), &parsed, &error);
  if (U_FAILURE(error) || parsed != static_cast<int32_t>(tag.size())) return false;
  id.assign(buffer.data(), length);
  return true;
}
JSValue canonical_locale(JSContext* context, const std::string& tag) {
  std::string id;
  if (!locale_id(tag, id)) return JS_ThrowRangeError(context, "invalid language tag");
  std::vector<char> buffer(id.size() + ULOC_FULLNAME_CAPACITY);
  UErrorCode error = U_ZERO_ERROR;
  const auto length = uloc_toLanguageTag(id.c_str(), buffer.data(),
                                         static_cast<int32_t>(buffer.size()), true, &error);
  if (U_FAILURE(error)) return JS_ThrowRangeError(context, "invalid language tag");
  return JS_NewStringLen(context, buffer.data(), length);
}
JSValue lookup_locale(JSContext* context, const std::string& tag) {
  // Segmenter has no relevant Unicode extension keys; matching uses the base
  // language tag. ICU locale data supplies language/script/region availability.
  std::string base = tag.substr(0, tag.find("-u-"));
  for (;;) {
    std::string id;
    if (!locale_id(base, id)) return JS_ThrowRangeError(context, "invalid language tag");
    for (int32_t i = 0; i < uloc_countAvailable(); ++i)
      if (id == uloc_getAvailable(i)) return JS_NewString(context, base.c_str());
    const auto last = base.rfind('-');
    if (last == std::string::npos) return JS_NULL;
    base.resize(last);
    // Remove a dangling extension singleton with its final subtag.
    if (base.size() >= 2 && base[base.size() - 2] == '-') base.resize(base.size() - 2);
  }
}
JSValue segment_record(JSContext* context, SegmentCursor& cursor, int32_t start, int32_t end) {
  JsValueOwner result(context, JS_NewArray(context));
  if (JS_IsException(result.get())) return JS_EXCEPTION;
  if (JS_SetPropertyUint32(context, result.get(), 0, JS_NewInt32(context, start)) < 0 ||
      JS_SetPropertyUint32(context, result.get(), 1, JS_NewInt32(context, end)) < 0 ||
      JS_SetPropertyUint32(
          context, result.get(), 2,
          JS_NewBool(context, cursor.word && ubrk_getRuleStatus(cursor.iterator.get()) >=
                                                 UBRK_WORD_NUMBER)) < 0)
    return JS_EXCEPTION;
  return result.release();
}
}  // namespace

void install_segmenter_class(JSContext* context) {
  auto& state = *static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  JS_NewClassID(state.runtime, &state.segmenter_class);
  JSClassDef definition{};
  definition.class_name = "UnicodeSegmentCursor";
  definition.finalizer = finalize_cursor;
  if (JS_NewClass(state.runtime, state.segmenter_class, &definition) < 0)
    throw CliError("RUNTIME_INIT", "cannot register Unicode segment cursor");
}

JSValue js_segmenter(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  if (argc < 2) return JS_ThrowTypeError(context, "segmentation operation and argument required");
  const auto operation = js_string(context, argv[0]);
  if (operation == "canonical") return canonical_locale(context, js_string(context, argv[1]));
  if (operation == "lookup") return lookup_locale(context, js_string(context, argv[1]));
  if (operation == "create") {
    if (argc < 4) return JS_ThrowTypeError(context, "locale, granularity and text required");
    std::string locale;
    if (!locale_id(js_string(context, argv[1]), locale))
      return JS_ThrowRangeError(context, "invalid language tag");
    const auto granularity = js_string(context, argv[2]);
    const auto kind = granularity == "grapheme" ? UBRK_CHARACTER
                      : granularity == "word"   ? UBRK_WORD
                                                : UBRK_SENTENCE;
    if (granularity != "grapheme" && granularity != "word" && granularity != "sentence")
      return JS_ThrowRangeError(context, "invalid segmentation granularity");
    size_t length = 0;
    const auto* data = JS_ToCStringLenUTF16(context, &length, argv[3]);
    if (!data) return JS_EXCEPTION;
    const auto free_text = [context](const uint16_t* value) {
      JS_FreeCStringUTF16(context, value);
    };
    const std::unique_ptr<const uint16_t, decltype(free_text)> text(data, free_text);
    if (length > static_cast<size_t>(std::numeric_limits<int32_t>::max()))
      return JS_ThrowRangeError(context, "text exceeds ICU's UTF-16 index range");
    auto cursor = std::make_unique<SegmentCursor>();
    cursor->text = std::make_shared<std::vector<UChar>>(data, data + length);
    cursor->word = kind == UBRK_WORD;
    UErrorCode error = U_ZERO_ERROR;
    cursor->iterator.reset(ubrk_open(kind, locale.c_str(), cursor->text->data(),
                                     static_cast<int32_t>(length), &error));
    if (U_FAILURE(error)) return icu_error(context, error);
    ubrk_first(cursor->iterator.get());
    return cursor_object(context, std::move(cursor));
  }
  const auto* state = static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  auto* cursor =
      static_cast<SegmentCursor*>(JS_GetOpaque2(context, argv[1], state->segmenter_class));
  if (!cursor) return JS_EXCEPTION;
  if (operation == "clone") {
    auto copy = std::make_unique<SegmentCursor>();
    copy->text = cursor->text;
    copy->word = cursor->word;
    UErrorCode error = U_ZERO_ERROR;
    copy->iterator.reset(ubrk_clone(cursor->iterator.get(), &error));
    if (U_FAILURE(error)) return icu_error(context, error);
    ubrk_first(copy->iterator.get());
    return cursor_object(context, std::move(copy));
  }
  if (operation == "next") {
    const auto start = ubrk_current(cursor->iterator.get());
    const auto end = ubrk_next(cursor->iterator.get());
    return end == UBRK_DONE ? JS_NULL : segment_record(context, *cursor, start, end);
  }
  if (operation == "containing") {
    if (argc < 3) return JS_ThrowTypeError(context, "UTF-16 index required");
    int32_t index;
    if (JS_ToInt32(context, &index, argv[2]) < 0) return JS_EXCEPTION;
    if (index < 0 || static_cast<size_t>(index) >= cursor->text->size()) return JS_UNDEFINED;
    const auto start = ubrk_preceding(cursor->iterator.get(), index + 1);
    const auto end = ubrk_following(cursor->iterator.get(), index);
    return segment_record(context, *cursor, start, end);
  }
  return JS_ThrowTypeError(context, "unknown segmentation operation");
}
}  // namespace webcli
