#include <charconv>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string_view>

#include "core/errors.hpp"
#include "core/serialization/json_string.hpp"
#include "core/text.hpp"
#include "storage/profile_store.hpp"
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <cerrno>
#endif

namespace webcli {
namespace {
constexpr std::uintmax_t kLogBytes = 8 * 1024 * 1024;
constexpr auto kLogAge = std::chrono::hours(24 * 7);
std::mutex log_mutex;

// Separate workers share diagnostics. Hold this lock across rotation and append
// so another process cannot rename a log while a writer still has it open.
class LogLock {
 public:
  explicit LogLock(const std::filesystem::path& path) {
#ifdef _WIN32
    handle_ =
        CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle_ == INVALID_HANDLE_VALUE)
      throw CliError("LOG_WRITE_FAILED", "could not open diagnostic lock");
    if (!LockFileEx(handle_, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &position_)) {
      CloseHandle(handle_);
      throw CliError("LOG_WRITE_FAILED", "could not lock diagnostics");
    }
#else
    handle_ = open(path.c_str(), O_CREAT | O_RDWR, 0600);
    if (handle_ < 0) throw CliError("LOG_WRITE_FAILED", "could not open diagnostic lock");
    int result;
    do {
      result = flock(handle_, LOCK_EX);
    } while (result < 0 && errno == EINTR);
    if (result < 0) {
      close(handle_);
      throw CliError("LOG_WRITE_FAILED", "could not lock diagnostics");
    }
#endif
  }
  ~LogLock() {
#ifdef _WIN32
    UnlockFileEx(handle_, 0, 1, 0, &position_);
    CloseHandle(handle_);
#else
    flock(handle_, LOCK_UN);
    close(handle_);
#endif
  }
  LogLock(const LogLock&) = delete;
  LogLock& operator=(const LogLock&) = delete;

 private:
#ifdef _WIN32
  HANDLE handle_ = INVALID_HANDLE_VALUE;
  OVERLAPPED position_{};
#else
  int handle_ = -1;
#endif
};

bool expired(const std::filesystem::path& path, std::int64_t now) {
  // Read only a bounded prefix, even if an old or corrupt log has a huge line.
  std::ifstream file(path, std::ios::binary);
  char prefix[80]{};
  file.read(prefix, sizeof(prefix));
  const std::string_view text(prefix, static_cast<std::size_t>(file.gcount()));
  constexpr std::string_view key = "{\"timestamp_unix_ms\":";
  if (text.substr(0, key.size()) != key) return false;
  std::int64_t first = 0;
  const auto parsed = std::from_chars(text.data() + key.size(), text.data() + text.size(), first);
  const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(kLogAge).count();
  return parsed.ec == std::errc{} && first > 0 && now > first && now - first >= age;
}

void rotate(const std::filesystem::path& path) {
  const auto previous = std::filesystem::path(path.string() + ".1");
  const auto older = std::filesystem::path(path.string() + ".2");
  std::filesystem::remove(older);
  if (std::filesystem::exists(previous)) std::filesystem::rename(previous, older);
  if (std::filesystem::exists(path)) std::filesystem::rename(path, previous);
}
}  // namespace

void append_script_error_log(const Page& page, std::size_t first) {
  if (first >= page.script_errors.size()) return;
  std::lock_guard<std::mutex> guard(log_mutex);
  const auto dir = diagnostic_directory();
  std::filesystem::create_directories(dir);
  LogLock lock(dir / "runtime.lock");
  const auto path = dir / "runtime.jsonl";
  const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
  std::uintmax_t bytes = std::filesystem::exists(path) ? std::filesystem::file_size(path) : 0;
  // Old unbounded logs must not survive as oversized rotation backups.
  for (const auto& candidate : {path, std::filesystem::path(path.string() + ".1"),
                                std::filesystem::path(path.string() + ".2")}) {
    if (std::filesystem::exists(candidate) && std::filesystem::file_size(candidate) > kLogBytes)
      std::filesystem::remove(candidate);
  }
  if (bytes > kLogBytes) bytes = 0;
  if (bytes && expired(path, now)) {
    rotate(path);
    bytes = 0;
  }
  for (std::size_t i = first; i < page.script_errors.size(); ++i) {
    std::ostringstream record;
    record << "{\"timestamp_unix_ms\":" << now
           << ",\"documentId\":" << json_escape(page.document_id)
           << ",\"url\":" << json_escape(truncate(page.url, 8192))
           << ",\"category\":\"javascript\",\"message\":"
           << json_escape(truncate(page.script_errors[i], 1200)) << "}\n";
    const auto text = record.str();
    if (bytes && text.size() > kLogBytes - bytes) {
      rotate(path);
      bytes = 0;
    }
    std::ofstream log(path, std::ios::binary | std::ios::app);
    log.write(text.data(), static_cast<std::streamsize>(text.size()));
    log.flush();
    if (!log) throw CliError("LOG_WRITE_FAILED", "could not write diagnostic log");
    bytes += text.size();
  }
}
}  // namespace webcli
