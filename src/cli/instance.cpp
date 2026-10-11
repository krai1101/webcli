#include "cli/instance.hpp"

#include <chrono>
#include <fstream>
#include <sstream>

#include "core/errors.hpp"
#ifdef _WIN32
// Tool Help declarations require Windows types to be defined first.
// clang-format off
#include <windows.h>
#include <tlhelp32.h>
// clang-format on
#else
#include <unistd.h>
#endif

namespace webcli {
std::string automatic_instance(bool interactive) {
#ifdef _WIN32
  const auto own = GetCurrentProcessId();
#else
  const auto own = getpid();
#endif
  if (interactive) {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    return "prompt_" + std::to_string(own) + "_" + std::to_string(nonce);
  }
#ifdef _WIN32
  const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE)
    throw CliError("INSTANCE_INIT", "cannot identify the calling terminal");
  DWORD parent = 0;
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  if (Process32FirstW(snapshot, &entry)) {
    do {
      if (entry.th32ProcessID == own) {
        parent = entry.th32ParentProcessID;
        break;
      }
    } while (Process32NextW(snapshot, &entry));
  }
  CloseHandle(snapshot);
  if (!parent) throw CliError("INSTANCE_INIT", "cannot identify the calling terminal");
  std::string identity = "terminal_" + std::to_string(parent);
  const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, parent);
  if (process) {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (GetProcessTimes(process, &created, &exited, &kernel, &user)) {
      const auto stamp =
          (static_cast<unsigned long long>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
      identity += "_" + std::to_string(stamp);
    }
    CloseHandle(process);
  }
  return identity;
#else
  const auto parent = getppid();
  std::string identity = "terminal_" + std::to_string(parent);
#ifdef __linux__
  // Include the process start time so reused shell PIDs cannot inherit a page.
  std::ifstream stat("/proc/" + std::to_string(parent) + "/stat");
  std::string line;
  if (std::getline(stat, line)) {
    const auto end = line.rfind(')');
    if (end != std::string::npos) {
      std::istringstream fields(line.substr(end + 1));
      std::string field;
      for (int number = 3; number <= 22 && fields >> field; ++number)
        if (number == 22) identity += "_" + field;
    }
  }
#endif
  return identity;
#endif
}
}  // namespace webcli
