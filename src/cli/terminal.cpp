#include "cli/terminal.hpp"

#include <cstdlib>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace webcli {
namespace {
const char* environment(const char* name, char* buffer, std::size_t capacity) noexcept {
#ifdef _WIN32
  const auto size = GetEnvironmentVariableA(name, buffer, static_cast<DWORD>(capacity));
  return size && size < capacity ? buffer : nullptr;
#else
  (void)buffer;
  (void)capacity;
  return std::getenv(name);
#endif
}
}  // namespace
bool unicode_terminal() noexcept {
  char ascii_buffer[16]{}, term_buffer[64]{};
  const char* ascii = environment("WEBCLI_ASCII", ascii_buffer, sizeof(ascii_buffer));
  if (ascii && std::strcmp(ascii, "1") == 0) return false;
  const char* term = environment("TERM", term_buffer, sizeof(term_buffer));
  if (term && std::strcmp(term, "dumb") == 0) return false;
#ifdef _WIN32
  DWORD mode = 0;
  const auto handle = GetStdHandle(STD_OUTPUT_HANDLE);
  CONSOLE_SCREEN_BUFFER_INFO info{};
  return GetConsoleMode(handle, &mode) && GetConsoleOutputCP() == CP_UTF8 &&
         GetConsoleScreenBufferInfo(handle, &info) &&
         info.srWindow.Right - info.srWindow.Left + 1 >= 69;
#else
  if (!isatty(STDOUT_FILENO)) return false;
  winsize size{};
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col && size.ws_col < 69) return false;
  const char* locale = std::getenv("LC_ALL");
  if (!locale || !*locale) locale = std::getenv("LC_CTYPE");
  if (!locale || !*locale) locale = std::getenv("LANG");
  return locale && (std::strstr(locale, "UTF-8") || std::strstr(locale, "utf-8") ||
                    std::strstr(locale, "UTF8") || std::strstr(locale, "utf8"));
#endif
}
}  // namespace webcli
