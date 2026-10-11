#include "runtime/scripts/import_map.hpp"

#include <algorithm>
#include <vector>

#include "core/errors.hpp"
#include "network/data_script.hpp"
#include "network/http_client.hpp"
#include "runtime/engine/js_handles.hpp"

namespace webcli {
namespace {
bool url_like(const std::string& value) {
  return value.rfind("./", 0) == 0 || value.rfind("../", 0) == 0 || value.rfind("/", 0) == 0 ||
         value.find(':') != std::string::npos;
}
bool object(JSContext*, JSValueConst value) { return JS_IsObject(value) && JS_IsArray(value) == 0; }
class Properties {
 public:
  Properties(JSContext* context, JSValueConst value) : context_(context) {
    if (JS_GetOwnPropertyNames(context, &items, &count, value,
                               JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) < 0)
      throw CliError("IMPORT_MAP", "could not enumerate import map");
  }
  ~Properties() {
    for (uint32_t i = 0; i < count; ++i) JS_FreeAtom(context_, items[i].atom);
    js_free(context_, items);
  }
  Properties(const Properties&) = delete;
  Properties& operator=(const Properties&) = delete;
  JSPropertyEnum* items = nullptr;
  uint32_t count = 0;

 private:
  JSContext* context_;
};
std::string property_key(JSContext* context, JSAtom atom) {
  const JsValueOwner key(context, JS_AtomToString(context, atom));
  return js_string(context, key.get());
}
JSValue json_property(JSContext* context, JSValueConst object, const char* name) {
  Properties properties(context, object);
  for (uint32_t i = 0; i < properties.count; ++i)
    if (property_key(context, properties.items[i].atom) == name)
      return JS_GetProperty(context, object, properties.items[i].atom);
  // Do not consult a prototype modified by earlier page scripts.
  return JS_UNDEFINED;
}
std::string normalize_dot_segments(std::string suffix) {
  // WHATWG URLs recognize percent-encoded dots in dot-only path segments;
  // curl's URL resolver otherwise leaves those segments escaped.
  const auto separator = suffix.find_first_of("?#");
  const auto end = separator == std::string::npos ? suffix.size() : separator;
  std::string result;
  std::size_t start = 0;
  while (start < end) {
    auto stop = suffix.find('/', start);
    stop = std::min(stop == std::string::npos ? end : stop, end);
    auto segment = suffix.substr(start, stop - start);
    std::string dots;
    for (std::size_t i = 0; i < segment.size(); ++i) {
      if (segment[i] == '%' && i + 2 < segment.size() && segment[i + 1] == '2' &&
          (segment[i + 2] == 'e' || segment[i + 2] == 'E')) {
        dots += '.';
        i += 2;
      } else
        dots += segment[i];
    }
    result += dots == "." || dots == ".." ? dots : segment;
    if (stop < end) result += '/';
    start = stop + 1;
  }
  return result + suffix.substr(end);
}
}  // namespace

ImportMap::Specifiers ImportMap::parse_specifiers(JSContext* context, JSValueConst value,
                                                  const std::string& base, std::size_t& entries) {
  if (!object(context, value))
    throw CliError("IMPORT_MAP", "imports and scope mappings must be JSON objects");
  Properties properties(context, value);
  Specifiers result;
  for (uint32_t i = 0; i < properties.count; ++i) {
    if (++entries > 2048) throw CliError("IMPORT_MAP_QUOTA", "import map exceeds 2048 entries");
    auto key = property_key(context, properties.items[i].atom);
    if (key.empty()) continue;
    if (url_like(key) && key.rfind("blob:", 0) != 0 && !is_data_script_url(key)) {
      const auto normalized = resolve_web_url(base, key);
      if (!normalized) continue;
      key = *normalized;
    }
    const JsValueOwner address(context, JS_GetProperty(context, value, properties.items[i].atom));
    std::optional<std::string> target;
    if (JS_IsString(address.get())) {
      const auto text = js_string(context, address.get());
      target = text.rfind("blob:", 0) == 0 || is_data_script_url(text)
                   ? std::optional<std::string>(text)
                   : resolve_web_url(base, text);
      if (target && key.back() == '/' && target->back() != '/') target.reset();
    }
    // Null/invalid targets block matching imports instead of falling through.
    result.emplace(std::move(key), std::move(target));
  }
  return result;
}

void ImportMap::add(JSContext* context, const std::string& source, const std::string& base) {
  if (resolved_)
    throw CliError("IMPORT_MAP_ORDER", "import maps after module resolution are unsupported");
  if (source.size() > 64 * 1024)
    throw CliError("IMPORT_MAP_QUOTA", "import map exceeds 65536 bytes");
  const JsValueOwner parsed(context,
                            JS_ParseJSON(context, source.data(), source.size(), "import map"));
  if (JS_IsException(parsed.get())) {
    const JsValueOwner exception(context, JS_GetException(context));
    throw CliError("IMPORT_MAP", "invalid import map JSON: " + js_string(context, exception.get()));
  }
  if (!object(context, parsed.get()))
    throw CliError("IMPORT_MAP", "import map must be a JSON object");
  // Stage the complete registration so quota/type failures leave the old map intact.
  auto entries = entries_;
  Specifiers imports;
  std::map<std::string, Specifiers> scopes;
  const JsValueOwner import_value(context, json_property(context, parsed.get(), "imports"));
  if (!JS_IsUndefined(import_value.get()))
    imports = parse_specifiers(context, import_value.get(), base, entries);
  const JsValueOwner scope_value(context, json_property(context, parsed.get(), "scopes"));
  if (!JS_IsUndefined(scope_value.get())) {
    if (!object(context, scope_value.get()))
      throw CliError("IMPORT_MAP", "scopes must be a JSON object");
    Properties properties(context, scope_value.get());
    if (properties.count + scopes_.size() > 128)
      throw CliError("IMPORT_MAP_QUOTA", "import map exceeds 128 scopes");
    for (uint32_t i = 0; i < properties.count; ++i) {
      auto scope = resolve_web_url(base, property_key(context, properties.items[i].atom));
      if (!scope) continue;
      const JsValueOwner mappings(
          context, JS_GetProperty(context, scope_value.get(), properties.items[i].atom));
      scopes.emplace(*scope, parse_specifiers(context, mappings.get(), base, entries));
    }
  }
  // Earlier mappings win when multiple initial import maps register.
  imports_.insert(imports.begin(), imports.end());
  for (const auto& scope : scopes)
    scopes_[scope.first].insert(scope.second.begin(), scope.second.end());
  entries_ = entries;
}

std::optional<std::string> ImportMap::match(const Specifiers& entries, const std::string& key) {
  auto exact = entries.find(key);
  if (exact != entries.end()) {
    if (!exact->second) throw CliError("IMPORT_MAP_BLOCKED", "blocked module specifier: " + key);
    return exact->second;
  }
  const Specifiers::value_type* best = nullptr;
  for (const auto& entry : entries)
    if (entry.first.back() == '/' && key.rfind(entry.first, 0) == 0 &&
        (!best || entry.first.size() > best->first.size()))
      best = &entry;
  if (!best) return std::nullopt;
  if (!best->second) throw CliError("IMPORT_MAP_BLOCKED", "blocked module specifier: " + key);
  const auto target =
      resolve_web_url(*best->second, normalize_dot_segments(key.substr(best->first.size())));
  if (!target || target->rfind(*best->second, 0) != 0)
    throw CliError("IMPORT_MAP_BLOCKED", "module prefix mapping escapes its target: " + key);
  return target;
}

std::string ImportMap::resolve(const std::string& specifier, const std::string& referrer) {
  resolved_ = true;
  const auto url = is_data_script_url(specifier) || specifier.rfind("blob:", 0) == 0
                       ? std::optional<std::string>(specifier)
                   : specifier.rfind("http://", 0) == 0 || specifier.rfind("https://", 0) == 0
                       ? resolve_web_url(specifier, specifier)
                   : url_like(specifier) ? resolve_web_url(referrer, specifier)
                                         : std::nullopt;
  const auto key = url.value_or(specifier);
  std::vector<const std::pair<const std::string, Specifiers>*> scopes;
  for (const auto& scope : scopes_)
    if (referrer == scope.first ||
        (scope.first.back() == '/' && referrer.rfind(scope.first, 0) == 0))
      scopes.push_back(&scope);
  std::sort(scopes.begin(), scopes.end(),
            [](const auto* a, const auto* b) { return a->first.size() > b->first.size(); });
  for (const auto* scope : scopes)
    if (auto target = match(scope->second, key)) return *target;
  if (auto target = match(imports_, key)) return *target;
  if (url) return *url;
  throw CliError(
      "MODULE_SPECIFIER",
      "bare module specifiers require an import map, or URL is unsupported: " + specifier);
}
}  // namespace webcli
