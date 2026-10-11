#pragma once
#include <quickjs.h>

#include <string>

#include "core/errors.hpp"

namespace webcli {
class JsValueOwner {
 public:
  JsValueOwner(JSContext* context, JSValue value) : context_(context), value_(value) {}
  ~JsValueOwner() { JS_FreeValue(context_, value_); }
  JsValueOwner(const JsValueOwner&) = delete;
  JsValueOwner& operator=(const JsValueOwner&) = delete;
  JSValue get() const { return value_; }
  JSValue release() {
    auto value = value_;
    value_ = JS_UNDEFINED;
    return value;
  }

 private:
  JSContext* context_;
  JSValue value_;
};
class JsCString {
 public:
  JsCString(JSContext* context, JSValueConst value) : context_(context) {
    value_ = JS_ToCStringLen(context, &size_, value);
    if (!value_) throw CliError("JS_CONVERSION", "could not convert JavaScript string");
  }
  ~JsCString() { JS_FreeCString(context_, value_); }
  JsCString(const JsCString&) = delete;
  JsCString& operator=(const JsCString&) = delete;
  const char* get() const { return value_; }
  std::size_t size() const { return size_; }

 private:
  JSContext* context_;
  const char* value_ = nullptr;
  std::size_t size_ = 0;
};
inline std::string js_string(JSContext* context, JSValueConst value) {
  const JsCString text(context, value);
  return {text.get(), text.size()};
}
inline std::string property_string(JSContext* context, JSValueConst object, const char* name,
                                   const std::string& fallback = "") {
  const JsValueOwner value(context, JS_GetPropertyStr(context, object, name));
  if (JS_IsException(value.get()))
    throw CliError("JS_PROPERTY", "could not read JavaScript property");
  return JS_IsUndefined(value.get()) || JS_IsNull(value.get()) ? fallback
                                                               : js_string(context, value.get());
}
}  // namespace webcli
