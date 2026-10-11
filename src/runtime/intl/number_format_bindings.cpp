#include "runtime/intl/number_format_bindings.hpp"

#include <unicode/ucurr.h>
#include <unicode/ufieldpositer.h>
#include <unicode/uloc.h>
#include <unicode/unum.h>
#include <unicode/unumberformatter.h>
#include <unicode/unumsys.h>

#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

#include "core/errors.hpp"
#include "runtime/engine/js_handles.hpp"
#include "runtime/engine/script_context.hpp"

namespace webcli {
namespace {
using Formatter = std::unique_ptr<UNumberFormatter, decltype(&unumf_close)>;
struct NumberFormatter {
  Formatter formatter{nullptr, unumf_close};
};
void finalize(JSRuntime*, JSValue value) {
  delete static_cast<NumberFormatter*>(JS_GetOpaque(value, JS_GetClassID(value)));
}
const char* field_name(int field, bool negative, double number) {
  switch (field) {
    case UNUM_INTEGER_FIELD:
      return std::isnan(number) ? "nan" : std::isinf(number) ? "infinity" : "integer";
    case UNUM_FRACTION_FIELD:
      return "fraction";
    case UNUM_DECIMAL_SEPARATOR_FIELD:
      return "decimal";
    case UNUM_GROUPING_SEPARATOR_FIELD:
      return "group";
    case UNUM_CURRENCY_FIELD:
      return "currency";
    case UNUM_PERCENT_FIELD:
      return "percentSign";
    case UNUM_SIGN_FIELD:
      return negative ? "minusSign" : "plusSign";
    case UNUM_EXPONENT_SYMBOL_FIELD:
      return "exponentSeparator";
    case UNUM_EXPONENT_SIGN_FIELD:
      return "exponentMinusSign";
    case UNUM_EXPONENT_FIELD:
      return "exponentInteger";
    case UNUM_COMPACT_FIELD:
      return "compact";
    default:
      return "literal";
  }
}
JSValue failure(JSContext* context, UErrorCode status) {
  return JS_ThrowRangeError(context, "number formatting failed: %s", u_errorName(status));
}
}  // namespace
void install_number_format_class(JSContext* context) {
  auto& state = *static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  JS_NewClassID(state.runtime, &state.number_format_class);
  JSClassDef definition{};
  definition.class_name = "LocaleNumberFormatter";
  definition.finalizer = finalize;
  if (JS_NewClass(state.runtime, state.number_format_class, &definition) < 0)
    throw CliError("RUNTIME_INIT", "cannot register locale number formatter");
}
JSValue js_number_format(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  if (argc < 2) return JS_ThrowTypeError(context, "format operation and arguments required");
  const auto operation = js_string(context, argv[0]);
  UErrorCode status = U_ZERO_ERROR;
  if (operation == "currencyDigits") {
    const auto code = js_string(context, argv[1]);
    if (code.size() != 3) return JS_ThrowRangeError(context, "invalid currency code");
    const UChar currency[] = {static_cast<UChar>(code[0]), static_cast<UChar>(code[1]),
                              static_cast<UChar>(code[2]), 0};
    const int digits = ucurr_getDefaultFractionDigits(currency, &status);
    return U_FAILURE(status) ? failure(context, status) : JS_NewInt32(context, digits);
  }
  if (operation == "create" || operation == "numberingSystem") {
    if (argc < 3) return JS_ThrowTypeError(context, "locale and format skeleton required");
    const auto tag = js_string(context, argv[1]);
    if (tag.size() > 1024 || tag.find('\0') != std::string::npos)
      return JS_ThrowRangeError(context, "invalid locale");
    std::vector<char> locale(tag.size() + ULOC_FULLNAME_CAPACITY);
    int32_t parsed = 0;
    uloc_forLanguageTag(tag.c_str(), locale.data(), static_cast<int32_t>(locale.size()), &parsed,
                        &status);
    if (U_FAILURE(status) || parsed != static_cast<int32_t>(tag.size()))
      return JS_ThrowRangeError(context, "invalid locale");
    if (operation == "numberingSystem") {
      const auto name = js_string(context, argv[2]);
      std::unique_ptr<UNumberingSystem, decltype(&unumsys_close)> system(
          name.empty() ? unumsys_open(locale.data(), &status)
                       : unumsys_openByName(name.c_str(), &status),
          unumsys_close);
      if (U_FAILURE(status)) return failure(context, status);
      return JS_NewString(context, unumsys_getName(system.get()));
    }
    size_t length = 0;
    const auto* skeleton = JS_ToCStringLenUTF16(context, &length, argv[2]);
    if (!skeleton) return JS_EXCEPTION;
    const auto free_text = [context](const uint16_t* text) { JS_FreeCStringUTF16(context, text); };
    std::unique_ptr<const uint16_t, decltype(free_text)> owned(skeleton, free_text);
    if (length > 1024)
      return JS_ThrowRangeError(context, "format skeleton exceeds supported options");
    auto formatter = std::make_unique<NumberFormatter>();
    formatter->formatter.reset(
        unumf_openForSkeletonAndLocale(reinterpret_cast<const UChar*>(skeleton),
                                       static_cast<int32_t>(length), locale.data(), &status));
    if (U_FAILURE(status)) return failure(context, status);
    auto& state = *static_cast<ScriptContext*>(JS_GetContextOpaque(context));
    JSValue result = JS_NewObjectClass(context, state.number_format_class);
    if (JS_IsException(result)) return result;
    JS_SetOpaque(result, formatter.release());
    return result;
  }
  if (operation != "parts" || argc < 3)
    return JS_ThrowTypeError(context, "invalid formatting operation");
  auto& state = *static_cast<ScriptContext*>(JS_GetContextOpaque(context));
  const auto* formatter =
      static_cast<NumberFormatter*>(JS_GetOpaque2(context, argv[1], state.number_format_class));
  if (!formatter) return JS_EXCEPTION;
  std::unique_ptr<UFormattedNumber, decltype(&unumf_closeResult)> formatted(
      unumf_openResult(&status), unumf_closeResult);
  double number = 0;
  bool negative = false;
  if (JS_IsString(argv[2])) {
    const JsCString text(context, argv[2]);
    negative = text.size() && text.get()[0] == '-';
    unumf_formatDecimal(formatter->formatter.get(), text.get(), static_cast<int32_t>(text.size()),
                        formatted.get(), &status);
  } else {
    if (JS_ToFloat64(context, &number, argv[2]) < 0) return JS_EXCEPTION;
    negative = std::signbit(number);
    unumf_formatDouble(formatter->formatter.get(), number, formatted.get(), &status);
  }
  if (U_FAILURE(status)) return failure(context, status);
  int32_t length = unumf_resultToString(formatted.get(), nullptr, 0, &status);
  if (status != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(status)) return failure(context, status);
  status = U_ZERO_ERROR;
  std::vector<UChar> text(length + 1);
  unumf_resultToString(formatted.get(), text.data(), static_cast<int32_t>(text.size()), &status);
  std::unique_ptr<UFieldPositionIterator, decltype(&ufieldpositer_close)> positions(
      ufieldpositer_open(&status), ufieldpositer_close);
  unumf_resultGetAllFieldPositions(formatted.get(), positions.get(), &status);
  if (U_FAILURE(status)) return failure(context, status);
  struct Field {
    int type, start, end;
  };
  std::vector<Field> fields;
  int32_t start = 0, end = 0;
  for (int field; (field = ufieldpositer_next(positions.get(), &start, &end)) >= 0;)
    fields.push_back({field, start, end});
  std::vector<const char*> types(length, "literal");
  // Integer spans include grouping separators: assign broad fields first.
  for (int pass = 0; pass < 2; ++pass)
    for (const auto& field : fields)
      if ((field.type == UNUM_INTEGER_FIELD) == (pass == 0))
        for (int i = field.start; i < field.end && i < length; ++i)
          types[i] = field_name(field.type, negative, number);
  JsValueOwner result(context, JS_NewArray(context));
  if (JS_IsException(result.get())) return JS_EXCEPTION;
  uint32_t index = 0;
  for (int i = 0; i < length;) {
    int until = i + 1;
    while (until < length && std::strcmp(types[i], types[until]) == 0) ++until;
    JsValueOwner part(context, JS_NewArray(context));
    if (JS_IsException(part.get()) ||
        JS_SetPropertyUint32(context, part.get(), 0, JS_NewString(context, types[i])) < 0 ||
        JS_SetPropertyUint32(
            context, part.get(), 1,
            JS_NewStringUTF16(context, reinterpret_cast<const uint16_t*>(text.data() + i),
                              until - i)) < 0 ||
        JS_SetPropertyUint32(context, result.get(), index++, part.release()) < 0)
      return JS_EXCEPTION;
    i = until;
  }
  return result.release();
}
}  // namespace webcli
