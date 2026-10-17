#include "cli/commands.hpp"

#include <algorithm>
#include <cctype>
#include <limits>

#include "core/errors.hpp"

namespace webcli {
const std::vector<CommandSpec>& command_specs() {
  static const std::vector<CommandSpec> specs{
      {"open", "open <url>", "Navigate to a URL", 1, 1},
      {"back", "back", "Go to the previous history entry and show the page", 0, 0},
      {"forward", "forward", "Go to the next history entry and show the page", 0, 0},
      {"observe", "observe", "Current page text and actionable refs; no reload", 0, 0},
      {"click", "click <ref>", "Click, wait for load/network idle, then show the page", 1, 1},
      {"fill", "fill <I-ref> <value>", "Fill a field; true/false for checkbox/radio", 2, 2},
      {"select", "select <S-ref> <value>", "Select an option by its value", 2, 2},
      {"keyboard", "keyboard <ref> <key>", "Send Enter, Escape, Tab or Shift+Tab to a ref", 2, 2},
      {"wait", "wait [idle|text|gone|ref|selector] [value]",
       "Wait for a condition; bounded by --timeout", 0, 2},
      {"close", "close", "Close this browsing instance; keep cookies/localStorage", 0, 0},
      {"help", "help", "Show commands and options", 0, 0},
      {"update", "update", "Update a script-managed installation to the latest stable release", 0, 0},
      {"uninstall", "uninstall", "Remove a script-managed installation; keep saved browsing data", 0, 0}};
  return specs;
}
namespace {
std::size_t number(const std::string& value, std::size_t maximum, const char* option) {
  if (value.empty() || !std::all_of(value.begin(), value.end(),
                                    [](unsigned char c) { return c >= '0' && c <= '9'; }))
    throw CliError("INVALID_ARGUMENT", std::string(option) + " requires a nonnegative integer");
  std::size_t result = 0;
  for (char c : value) {
    const auto digit = static_cast<std::size_t>(c - '0');
    if (result > maximum / 10 || result * 10 > maximum - digit)
      throw CliError("INVALID_ARGUMENT", std::string(option) + " is out of range");
    result = result * 10 + digit;
  }
  return result;
}
}  // namespace
Request parse_request(const std::vector<std::string>& tokens) {
  Request request;
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    const auto& token = tokens[i];
    if (token == "--json")
      request.json = true;
    else if (token == "--debug")
      request.debug = true;
    else if (token == "--help" || token == "-h" || token == "?")
      request.command = "help";
    else if (token == "--offset" || token == "--limit" || token == "--timeout" ||
             token == "--document") {
      if (++i >= tokens.size()) throw CliError("INVALID_ARGUMENT", token + " requires a value");
      if (token == "--document")
        request.document = tokens[i];
      else if (token == "--offset") {
        request.offset = number(tokens[i], 4000, "--offset");
        request.offset_set = true;
      } else if (token == "--limit") {
        request.limit = number(tokens[i], 200, "--limit");
        if (!request.limit) throw CliError("INVALID_ARGUMENT", "--limit must be 1..200");
      } else {
        request.timeout_ms = static_cast<unsigned>(number(tokens[i], 30000, "--timeout"));
        if (!request.timeout_ms)
          throw CliError("INVALID_ARGUMENT", "--timeout must be 1..30000 ms");
      }
    } else if (token.rfind("--", 0) == 0)
      throw CliError("UNKNOWN_OPTION", "unknown option " + token);
    else if (request.command.empty())
      request.command = token;
    else
      request.arguments.push_back(token);
  }
  if (!request.command.empty()) validate_request(request);
  return request;
}
void validate_request(const Request& request) {
  if (request.instance.size() > 64 ||
      !std::all_of(request.instance.begin(), request.instance.end(),
                   [](unsigned char c) { return std::isalnum(c) || c == '_' || c == '-'; }))
    throw CliError("INVALID_INSTANCE", "invalid browsing instance identifier");
  auto spec = std::find_if(command_specs().begin(), command_specs().end(),
                           [&](const CommandSpec& s) { return request.command == s.name; });
  if (spec == command_specs().end())
    throw CliError("UNKNOWN_COMMAND", "unknown command '" + request.command + "'; use help");
  if (request.arguments.size() < spec->min_args || request.arguments.size() > spec->max_args)
    throw CliError("INVALID_ARGUMENT", "usage: " + std::string(spec->syntax));
  if (request.limit == 0 || request.limit > 200 || request.offset > 4000 ||
      request.timeout_ms == 0 || request.timeout_ms > 30000)
    throw CliError("INVALID_ARGUMENT", "command options exceed their limits");
}
EngineRequest engine_request(const Request& request) {
  validate_request(request);
  EngineRequest result;
  result.document = request.document;
  result.timeout_ms = request.timeout_ms;
  if (request.command == "open")
    result.action = BrowserAction::Open;
  else if (request.command == "back")
    result.action = BrowserAction::Back;
  else if (request.command == "forward")
    result.action = BrowserAction::Forward;
  else if (request.command == "observe")
    result.action = BrowserAction::Observe;
  else if (request.command == "click")
    result.action = BrowserAction::Click;
  else if (request.command == "fill")
    result.action = BrowserAction::Fill;
  else if (request.command == "select")
    result.action = BrowserAction::Select;
  else if (request.command == "keyboard")
    result.action = BrowserAction::Keyboard;
  else if (request.command == "wait")
    result.action = BrowserAction::Wait;
  else
    throw CliError("UNKNOWN_COMMAND", "command is not a browsing action");
  if (result.action == BrowserAction::Wait) {
    const auto mode = request.arguments.empty() ? "idle" : request.arguments[0];
    if (mode == "idle")
      result.condition = WaitCondition::Idle;
    else if (mode == "text")
      result.condition = WaitCondition::Text;
    else if (mode == "gone")
      result.condition = WaitCondition::Gone;
    else if (mode == "ref")
      result.condition = WaitCondition::Reference;
    else if (mode == "selector")
      result.condition = WaitCondition::Selector;
    else
      throw CliError("INVALID_ARGUMENT", "wait expects idle, text, gone, ref or selector");
  } else if (!request.arguments.empty())
    result.target = request.arguments[0];
  if (request.arguments.size() > 1) result.value = request.arguments[1];
  validate_engine_request(result);
  return result;
}
std::vector<std::string> split_prompt_line(const std::string& line) {
  std::vector<std::string> tokens;
  std::string token;
  char quote = 0;
  bool escaped = false, started = false;
  for (char c : line) {
    if (escaped) {
      if (c != quote && c != '\\') token += '\\';
      token += c;
      escaped = false;
      started = true;
      continue;
    }
    if (quote && c == '\\') {
      escaped = true;
      continue;
    }
    if (quote) {
      if (c == quote)
        quote = 0;
      else
        token += c;
      started = true;
    } else if (c == '\'' || c == '"') {
      quote = c;
      started = true;
    } else if (std::isspace(static_cast<unsigned char>(c))) {
      if (started) {
        tokens.push_back(std::move(token));
        token.clear();
        started = false;
      }
    } else {
      token += c;
      started = true;
    }
  }
  if (escaped || quote) throw CliError("INVALID_ARGUMENT", "incomplete quoted value");
  if (started) tokens.push_back(std::move(token));
  return tokens;
}
void show_help(std::ostream& out) {
  out << "WebCLI - agent browsing\n\n";
  out << "Browsing commands:\n";
  for (const auto& spec : command_specs()) {
    if (std::string(spec.name) == "close") out << "\nUtilities:\n";
    if (std::string(spec.name) == "update") out << "\nInstallation (run from your terminal):\n";
    out << "  " << spec.syntax << "\n      " << spec.description << "\n";
  }
  out << "\nOptions (terminal and prompt):\n"
         "  --json             Machine-readable observation or error\n"
         "  --debug            Runtime activity, pending work, HTTP results and JavaScript errors\n"
         "  --offset N         Content block offset (default: linked section or main content)\n"
         "  --limit N          Content blocks to return (1..200, default: 80)\n"
         "  --timeout MS       Wait timeout (1..30000, default: 5000)\n"
         "  --document ID      Reject actions on a different document\n\n"
         "Refs: [L1] link, [B1] button, [I1] input, [S1] select. Use refs from current output.\n"
         "Examples: open example.com | click B1 | fill I1 \"Ada Lovelace\" | select S1 manager\n"
         "          keyboard I1 Enter | wait text \"Ready\" --timeout 10000 | observe --offset 80\n"
         "Keyboard keys: Enter, Escape, Tab, Shift+Tab (case-sensitive).\n"
         "Run webcli without a command for the prompt. Type quit/exit to leave it.\n"
         "Default prompts have independent pages and share saved default cookies/storage.\n"
         "Default terminal commands reuse the calling shell's page.\n"
         "Quit closes the prompt's live page; saved cookies/storage remain.\n";
}
}  // namespace webcli
