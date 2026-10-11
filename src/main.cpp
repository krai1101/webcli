#include <curl/curl.h>
#include <uv.h>

#include <algorithm>
#include <iostream>

#include "cli/commands.hpp"
#include "cli/instance.hpp"
#include "cli/output/format.hpp"
#include "core/errors.hpp"
#include "host/resident.hpp"
#include "network/cookie_store.hpp"
#include "network/http_handles.hpp"
#include "network/resource_service.hpp"
#ifdef _WIN32
#include <crtdbg.h>
#include <shellapi.h>
#include <windows.h>

#include <cstdlib>
#endif
namespace {
struct CurlGlobal {
  CurlGlobal() {
    webcli::initialize_http_transport();
  }
  ~CurlGlobal() {
    webcli::shutdown_resource_service();
    webcli::shutdown_http_handles();
    webcli::shutdown_cookie_store();
    curl_global_cleanup();
  }
};
}  // namespace
int main(int argc, char** argv) {
  uv_disable_stdio_inheritance();
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  _set_error_mode(_OUT_TO_STDERR);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#ifdef _DEBUG
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
#endif
  std::vector<std::string> tokens;
#ifdef _WIN32
  (void)argc;
  (void)argv;
  int count = 0;
  auto arguments = CommandLineToArgvW(GetCommandLineW(), &count);
  if (!arguments) {
    std::cerr << "Cannot read command line\n";
    return 1;
  }
  for (int i = 1; i < count; ++i) {
    const int length =
        WideCharToMultiByte(CP_UTF8, 0, arguments[i], -1, nullptr, 0, nullptr, nullptr);
    std::string value(length, '\0');
    WideCharToMultiByte(CP_UTF8, 0, arguments[i], -1, value.data(), length, nullptr, nullptr);
    value.resize(length - 1);
    tokens.push_back(std::move(value));
  }
  LocalFree(arguments);
#else
  tokens.assign(argv + 1, argv + argc);
#endif
  bool json = std::find(tokens.begin(), tokens.end(), "--json") != tokens.end();
  try {
    CurlGlobal curl;
    if (tokens.size() == 1 && tokens[0] == "--worker") return webcli::serve_browser();
    if (tokens.size() == 1 && tokens[0] == "--version") {
      std::cout << "WebCLI " << WEBCLI_VERSION << '\n';
      return 0;
    }
    auto request = webcli::parse_request(tokens);
    json = request.json;
    if (request.command == "help") {
      webcli::show_help(std::cout);
      return 0;
    }
    request.instance = webcli::automatic_instance(request.command.empty());
    if (request.command.empty()) return webcli::run_prompt(request);
    const auto response = webcli::send_request(request);
    std::cout << response.output;
    std::cerr << response.error;
    return response.status;
  } catch (const webcli::CliError& error) {
    webcli::render_error(json ? std::cout : std::cerr, json, error.code, error.what(),
                         error.retryable);
  } catch (const std::exception& error) {
    webcli::render_error(json ? std::cout : std::cerr, json, "INTERNAL_ERROR", error.what());
  }
  return 1;
}
