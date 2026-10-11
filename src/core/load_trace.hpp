#pragma once

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace webcli {
// Optional phase timing for engine diagnosis. No URL, cookies or page data are
// recorded. Disabled by default; it does not affect observation output.
class LoadTrace {
 public:
  explicit LoadTrace(const char* owner) : owner_(owner) {
#ifdef _WIN32
    char* setting = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&setting, &size, "WEBCLI_TRACE_LOAD") == 0) {
      enabled_ = setting && std::strcmp(setting, "1") == 0;
      std::free(setting);
    }
#else
    const char* setting = std::getenv("WEBCLI_TRACE_LOAD");
    enabled_ = setting && std::strcmp(setting, "1") == 0;
#endif
  }
  void mark(const char* phase) noexcept {
    if (!enabled_) return;
    const auto now = Clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_).count();
    std::fprintf(stderr, "webcli load phase: %s/%s %lld ms\n", owner_, phase,
                 static_cast<long long>(elapsed));
    std::fflush(stderr);
    last_ = now;
  }

 private:
  using Clock = std::chrono::steady_clock;
  const char* owner_;
  bool enabled_ = false;
  Clock::time_point last_ = Clock::now();
};
}  // namespace webcli
