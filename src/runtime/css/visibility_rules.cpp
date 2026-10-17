#include "runtime/css/visibility_rules.hpp"

#include <lexbor/css/css.h>
#include <lexbor/selectors/selectors.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <regex>
#include <tuple>
#include <unordered_set>

#include "core/errors.hpp"
#include "core/text.hpp"
#include "runtime/css/viewport.hpp"
#include "runtime/dom/dom_helpers.hpp"
namespace webcli {
namespace {
std::string lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return text;
}
struct Declaration {
  std::string value;
  bool important = false;
};
struct Rule {
  std::string selector;
  Declaration display, visibility, opacity;
};
bool numeric_opacity(const std::string& value) {
  if (value.empty() || value.find_first_not_of("0123456789.+-eE%") != std::string::npos)
    return false;
  char* end = nullptr;
  const auto number = std::strtod(value.c_str(), &end);
  return end != value.c_str() && std::isfinite(number) &&
         (*end == '\0' || (*end == '%' && end[1] == '\0'));
}
void declarations(const std::string& source, Declaration& display, Declaration& visibility,
                  Declaration& opacity) {
  static const std::unordered_set<std::string> displays = {"none",
                                                           "contents",
                                                           "block",
                                                           "inline",
                                                           "inline-block",
                                                           "flow-root",
                                                           "flex",
                                                           "inline-flex",
                                                           "grid",
                                                           "inline-grid",
                                                           "table",
                                                           "inline-table",
                                                           "table-row",
                                                           "table-cell",
                                                           "table-row-group",
                                                           "table-column",
                                                           "table-column-group",
                                                           "table-header-group",
                                                           "table-footer-group",
                                                           "table-caption",
                                                           "list-item",
                                                           "inline flex",
                                                           "inline grid",
                                                           "block flow",
                                                           "inline flow",
                                                           "inherit",
                                                           "initial",
                                                           "unset"};
  for (std::size_t at = 0; at < source.size();) {
    auto end = source.find(';', at);
    auto entry = source.substr(at, end == std::string::npos ? end : end - at);
    auto colon = entry.find(':');
    if (colon != std::string::npos) {
      auto name = lower(trim(entry.substr(0, colon))), value = lower(trim(entry.substr(colon + 1)));
      bool important = false;
      auto bang = value.find('!');
      if (bang != std::string::npos && trim(value.substr(bang + 1)) == "important") {
        important = true;
        value = trim(value.substr(0, bang));
      }
      Declaration* target = name == "display"      ? &display
                            : name == "visibility" ? &visibility
                            : name == "opacity"    ? &opacity
                                                   : nullptr;
      bool valid = name == "opacity" ? numeric_opacity(value) || value == "inherit" ||
                                           value == "initial" || value == "unset"
                   : name == "display"
                       ? displays.count(value) > 0
                       : value == "visible" || value == "hidden" || value == "collapse" ||
                             value == "inherit" || value == "initial" || value == "unset";
      if (target && valid && (!target->important || important)) *target = {value, important};
    }
    if (end == std::string::npos) break;
    at = end + 1;
  }
}
bool media_range_matches(const std::string& clause) {
  static const std::string operand = R"((width|height|(?:\d+(?:\.\d+)?|\.\d+)(?:px|em|rem)))";
  static const std::regex range("^\\(\\s*" + operand + R"(\s*(<=|>=|<|>|=)\s*)" + operand +
                                R"((?:\s*(<=|>=|<|>|=)\s*)" + operand + R"()?\s*\)$)");
  std::smatch match;
  if (!std::regex_match(clause, match, range)) return false;
  const auto dimension = [](const std::string& token) {
    return token == "width" || token == "height";
  };
  const bool chained = match[4].matched;
  if (chained ? (!dimension(match[3]) || dimension(match[1]) || dimension(match[5]) ||
                 match[2].str()[0] != match[4].str()[0] || match[2] == "=")
              : dimension(match[1]) == dimension(match[3]))
    return false;
  const auto pixels = [](const std::string& token) {
    if (token == "width") return double(kDesktopViewportWidth);
    if (token == "height") return double(kDesktopViewportHeight);
    return std::stod(token) *
           (token.size() >= 2 && token.substr(token.size() - 2) == "px" ? 1 : 16);
  };
  const auto compare = [](double left, const std::string& op, double right) {
    return op == "<"    ? left < right
           : op == "<=" ? left <= right
           : op == ">"  ? left > right
           : op == ">=" ? left >= right
                        : left == right;
  };
  try {
    return compare(pixels(match[1]), match[2], pixels(match[3])) &&
           (!chained || compare(pixels(match[3]), match[4], pixels(match[5])));
  } catch (const std::exception&) {
    return false;
  }
}
bool media_matches(std::string query) {
  query = lower(trim(query));
  if (query.empty()) return true;
  static const std::regex feature(R"(^\(([a-z-]+)\s*:\s*([^()]+)\)$)");
  static const std::regex length(R"(^(\d+(?:\.\d+)?)(px|em|rem)$)");
  for (std::size_t at = 0; at < query.size();) {
    const auto comma = query.find(',', at);
    auto part = trim(query.substr(at, comma == std::string::npos ? comma : comma - at));
    bool negative = part.rfind("not ", 0) == 0;
    if (negative) part = trim(part.substr(4));
    if (part.rfind("only ", 0) == 0) part = trim(part.substr(5));
    bool matches = true;
    if (!part.empty() && part.front() != '(') {
      const auto space = part.find(' ');
      auto type = part.substr(0, space);
      matches = type == "all" || type == "screen";
      part = space == std::string::npos ? "" : trim(part.substr(space + 1));
      if (part.rfind("and ", 0) == 0) part = trim(part.substr(4));
    }
    while (!part.empty()) {
      const auto and_at = part.find(" and ");
      auto clause = trim(part.substr(0, and_at));
      std::smatch found;
      if (!std::regex_match(clause, found, feature))
        matches &= media_range_matches(clause);
      else {
        const auto name = found[1].str(), value = trim(found[2].str());
        std::smatch size;
        if (name == "width" || name == "min-width" || name == "max-width" || name == "height" ||
            name == "min-height" || name == "max-height") {
          if (!std::regex_match(value, size, length))
            matches = false;
          else {
            double pixels = 0;
            try {
              pixels = std::stod(size[1].str()) * (size[2].str() == "px" ? 1 : 16);
            } catch (...) {
              matches = false;
            }
            const auto actual = name.find("width") != std::string::npos ? kDesktopViewportWidth
                                                                        : kDesktopViewportHeight;
            matches &= name.rfind("min-", 0) == 0   ? actual >= pixels
                       : name.rfind("max-", 0) == 0 ? actual <= pixels
                                                    : actual == pixels;
          }
        } else
          matches &= (name == "prefers-color-scheme" && value == "light") ||
                     (name == "prefers-reduced-motion" && value == "reduce") ||
                     (name == "orientation" && value == "landscape") ||
                     ((name == "hover" || name == "any-hover" || name == "pointer" ||
                       name == "any-pointer") &&
                      value == "none");
      }
      if (and_at == std::string::npos) break;
      part = trim(part.substr(and_at + 5));
    }
    if (negative ? !matches : matches) return true;
    if (comma == std::string::npos) break;
    at = comma + 1;
  }
  return false;
}
std::string without_comments(const std::string& input) {
  std::string source;
  char quote = 0;
  for (std::size_t i = 0; i < input.size(); ++i) {
    const auto c = input[i];
    if (quote) {
      source += c;
      if (c == '\\' && i + 1 < input.size())
        source += input[++i];
      else if (c == quote)
        quote = 0;
    } else if (c == '\'' || c == '"') {
      quote = c;
      source += c;
    } else if (c == '/' && i + 1 < input.size() && input[i + 1] == '*') {
      auto end = input.find("*/", i + 2);
      if (end == std::string::npos) break;
      i = end + 1;
      source += ' ';
    } else
      source += c;
  }
  return source;
}
void parse_rules(const std::string& source, std::vector<Rule>& rules, bool& truncated,
                 unsigned level = 0) {
  if (level > 16) {
    truncated = true;
    return;
  }
  for (std::size_t at = 0; at < source.size();) {
    if (rules.size() >= 2048) {
      truncated = true;
      return;
    }
    std::size_t open = at;
    char quote = 0;
    unsigned parens = 0;
    for (; open < source.size(); ++open) {
      auto c = source[open];
      if (quote) {
        if (c == '\\')
          ++open;
        else if (c == quote)
          quote = 0;
      } else if (c == '\'' || c == '"')
        quote = c;
      else if (c == '(')
        ++parens;
      else if (c == ')' && parens)
        --parens;
      else if (!parens && (c == '{' || c == ';'))
        break;
    }
    if (open == source.size()) break;
    if (source[open] == ';') {
      at = open + 1;
      continue;
    }
    auto header = trim(source.substr(at, open - at));
    std::size_t end = open + 1;
    unsigned depth = 1;
    quote = 0;
    for (; end < source.size() && depth; ++end) {
      auto c = source[end];
      if (quote) {
        if (c == '\\')
          ++end;
        else if (c == quote)
          quote = 0;
      } else if (c == '\'' || c == '"')
        quote = c;
      else if (c == '{')
        ++depth;
      else if (c == '}')
        --depth;
    }
    if (depth) break;
    auto body = source.substr(open + 1, end - open - 2);
    if (lower(header).rfind("@media", 0) == 0) {
      if (media_matches(header.substr(6))) parse_rules(body, rules, truncated, level + 1);
    } else if (!header.empty() && header[0] != '@' && header.size() <= 4096 &&
               body.find('{') == std::string::npos) {
      Rule rule;
      rule.selector = header;
      declarations(body, rule.display, rule.visibility, rule.opacity);
      if (!rule.display.value.empty() || !rule.visibility.value.empty() ||
          !rule.opacity.value.empty())
        rules.push_back(std::move(rule));
    }
    at = end;
  }
}
using Priority = std::tuple<bool, std::uint64_t, std::size_t>;
struct Cascaded {
  VisibilityValue value;
  Priority display{}, visibility{}, opacity{};
};
struct Cascade {
  std::unordered_map<lxb_dom_node_t*, Cascaded> nodes;
  const Rule* rule = nullptr;
  std::size_t order = 0;
  void apply(lxb_dom_node_t* node, std::uint64_t specificity) {
    auto& result = nodes[node];
    auto set = [&](const Declaration& declaration, std::string& value, Priority& previous) {
      const Priority priority{declaration.important, specificity, order};
      if (!declaration.value.empty() && priority >= previous) {
        value = declaration.value;
        previous = priority;
      }
    };
    set(rule->display, result.value.display, result.display);
    set(rule->visibility, result.value.visibility, result.visibility);
    set(rule->opacity, result.value.opacity, result.opacity);
  }
};
lxb_status_t match(lxb_dom_node_t* node, lxb_css_selector_specificity_t specificity,
                   void* opaque) noexcept {
  try {
    static_cast<Cascade*>(opaque)->apply(node, specificity);
    return LXB_STATUS_OK;
  } catch (...) {
    return LXB_STATUS_ERROR;
  }
}
struct Matcher {
  lxb_css_parser_t* parser = lxb_css_parser_create();
  lxb_selectors_t* selectors = lxb_selectors_create();
  ~Matcher() {
    if (selectors) lxb_selectors_destroy(selectors, true);
    if (parser) {
      lxb_css_parser_selectors_destroy(parser);
      lxb_css_parser_destroy(parser, true);
    }
  }
};
}  // namespace
VisibilityValues stylesheet_visibility(lxb_dom_node_t* scope,
                                       const std::vector<lxb_dom_node_t*>& nodes,
                                       const std::vector<StyleSource>& sources, bool& truncated) {
  std::vector<Rule> rules;
  std::size_t bytes = 0;
  for (const auto& source : sources) {
    if (source.source.size() > 2 * 1024 * 1024 - bytes) {
      truncated = true;
      continue;
    }
    bytes += source.source.size();
    if (media_matches(source.media)) parse_rules(without_comments(source.source), rules, truncated);
  }
  Cascade cascade;
  if (!rules.empty()) {
    Matcher matcher;
    if (!matcher.parser || !matcher.selectors ||
        lxb_css_parser_init(matcher.parser, nullptr) != LXB_STATUS_OK ||
        lxb_css_parser_selectors_init(matcher.parser) != LXB_STATUS_OK ||
        lxb_selectors_init(matcher.selectors) != LXB_STATUS_OK)
      throw CliError("CSS_INIT", "could not initialize visibility selector matcher");
    for (const auto& rule : rules) {
      cascade.rule = &rule;
      ++cascade.order;
      auto* list = lxb_css_selectors_parse(
          matcher.parser, reinterpret_cast<const lxb_char_t*>(rule.selector.data()),
          rule.selector.size());
      if (!list) continue;
      const auto status = lxb_selectors_find(matcher.selectors, scope, list, match, &cascade);
      lxb_css_selector_list_destroy(list);
      if (status != LXB_STATUS_OK) truncated = true;
    }
  }
  for (auto* node : nodes) {
    Rule inline_rule;
    declarations(without_comments(attr(node, "style")), inline_rule.display, inline_rule.visibility,
                 inline_rule.opacity);
    if (inline_rule.display.value.empty() && inline_rule.visibility.value.empty() &&
        inline_rule.opacity.value.empty())
      continue;
    cascade.rule = &inline_rule;
    ++cascade.order;
    cascade.apply(node, std::uint64_t{1} << 32);
  }
  VisibilityValues result;
  for (auto& entry : cascade.nodes) result.emplace(entry.first, std::move(entry.second.value));
  return result;
}
bool visibility_media_matches(const std::string& query) { return media_matches(query); }
std::string visibility_css_source(const std::string& source, bool& truncated) {
  std::vector<Rule> rules;
  parse_rules(without_comments(source), rules, truncated);
  std::string result;
  for (const auto& rule : rules) {
    result += rule.selector + "{";
    auto append = [&](const char* name, const Declaration& declaration) {
      if (declaration.value.empty()) return;
      result += std::string(name) + ":" + declaration.value;
      if (declaration.important) result += "!important";
      result += ";";
    };
    append("display", rule.display);
    append("visibility", rule.visibility);
    append("opacity", rule.opacity);
    result += "}";
  }
  return result;
}
bool stylesheet_display_none(lxb_dom_node_t* node, const VisibilityValues& values) {
  auto found = values.find(node);
  return found != values.end() && found->second.display == "none";
}
bool stylesheet_invisible(lxb_dom_node_t* node, const VisibilityValues& values,
                          const std::function<lxb_dom_node_t*(lxb_dom_node_t*)>& parent) {
  if (node && node->type != LXB_DOM_NODE_TYPE_ELEMENT) node = parent(node);
  // Opacity composites the entire subtree: a descendant's opacity:1 cannot
  // undo a fully transparent ancestor. Text observations omit unpainted content.
  // This is separate from inherited visibility, which descendants can override.
  for (auto* ancestor = node; ancestor; ancestor = parent(ancestor)) {
    const auto found = values.find(ancestor);
    if (found != values.end() && numeric_opacity(found->second.opacity) &&
        std::strtod(found->second.opacity.c_str(), nullptr) <= 0)
      return true;
  }
  for (; node; node = parent(node)) {
    auto found = values.find(node);
    if (found == values.end()) continue;
    const auto& visibility = found->second.visibility;
    if (visibility == "hidden" || visibility == "collapse") return true;
    if (visibility == "visible" || visibility == "initial") return false;
  }
  return false;
}
}  // namespace webcli
