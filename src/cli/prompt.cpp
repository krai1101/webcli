#include <algorithm>
#include <iostream>

#include "cli/commands.hpp"
#include "cli/output/format.hpp"
#include "cli/terminal.hpp"
#include "core/errors.hpp"
#include "host/resident.hpp"
namespace webcli {
namespace {
void show_welcome() {
  if (!unicode_terminal()) {
    std::cout << "\nWEBCLI\nCLI browser for AI agents\n\n";
    return;
  }
  constexpr std::size_t width = 67;
  std::string border;
  for (std::size_t i = 0; i < width; ++i) border += u8"═";
  // These fixed banner glyphs each occupy one terminal column.
  const auto row = [](const std::string& text) {
    const auto columns = static_cast<std::size_t>(std::count_if(
        text.begin(), text.end(), [](unsigned char c) { return (c & 0xC0) != 0x80; }));
    std::cout << u8"║" << text << std::string(columns < width ? width - columns : 0, ' ')
              << u8"║\n";
  };
  std::cout << '\n' << u8"╔" << border << u8"╗\n";
  row("");
  row(u8"   ██╗    ██╗███████╗██████╗  ██████╗██╗     ██╗");
  row(u8"   ██║    ██║██╔════╝██╔══██╗██╔════╝██║     ██║");
  row(u8"   ██║ █╗ ██║█████╗  ██████╔╝██║     ██║     ██║");
  row(u8"   ██║███╗██║██╔══╝  ██╔══██╗██║     ██║     ██║");
  row(u8"   ╚███╔███╔╝███████╗██████╔╝╚██████╗███████╗██║");
  row(u8"    ╚══╝╚══╝ ╚══════╝╚═════╝  ╚═════╝╚══════╝╚═╝");
  row("");
  row("   CLI browser for AI agents");
  row("");
  std::cout << u8"╚" << border << u8"╝\n\n";
}
}  // namespace

int run_prompt(const Request& startup) {
  if (!startup.json) show_welcome();
  std::string line;
  while ((startup.json || static_cast<bool>(std::cout << "webcli> ")) &&
         std::getline(std::cin, line)) {
    bool json = startup.json;
    try {
      auto tokens = split_prompt_line(line);
      if (tokens.empty()) continue;
      if (tokens.size() == 1 && (tokens[0] == "quit" || tokens[0] == "exit")) break;
      json = startup.json || std::find(tokens.begin(), tokens.end(), "--json") != tokens.end();
      auto request = parse_request(tokens);
      request.instance = startup.instance;
      request.json = json;
      request.debug = startup.debug || request.debug;
      json = request.json;
      if (request.command == "help") {
        show_help(std::cout);
        continue;
      }
      auto response = send_request(request);
      std::cout << response.output;
      std::cerr << response.error;
    } catch (const CliError& error) {
      render_error(json ? std::cout : std::cerr, json, error.code, error.what(), error.retryable);
    } catch (const std::exception& error) {
      render_error(json ? std::cout : std::cerr, json, "INTERNAL_ERROR", error.what());
    }
  }
  {
    auto close = startup;
    close.command = "close";
    close.arguments.clear();
    send_request(close);
  }
  return 0;
}
}  // namespace webcli
