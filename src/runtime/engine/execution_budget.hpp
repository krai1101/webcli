#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <optional>

#include "core/resource_limits.hpp"

namespace webcli {
// Execution time and operation wall time are separate: network waits may extend
// a task deadline, but never extend the enclosing load/operation deadline.
class ExecutionBudget {
 public:
  using Clock = std::chrono::steady_clock;
  static constexpr auto task_timeout = std::chrono::seconds(kJsTaskSeconds);
  static constexpr auto operation_timeout = std::chrono::seconds(kJsOperationSeconds);

  class Window {
   public:
    explicit Window(ExecutionBudget& budget)
        : budget_(budget), previous_(budget.window_), reported_(budget.window_reported_) {
      if (!budget.window_) {
        budget.window_ = Clock::now() + operation_timeout;
        budget.window_reported_ = false;
      }
    }
    ~Window() {
      budget_.window_ = previous_;
      if (!previous_) budget_.window_reported_ = reported_;
    }
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

   private:
    ExecutionBudget& budget_;
    std::optional<Clock::time_point> previous_;
    bool reported_;
  };

  void set_cancellation(std::shared_ptr<std::atomic<bool>> token) { cancelled_ = std::move(token); }
  bool cancelled() const { return cancelled_ && cancelled_->load(); }
  bool begin_task() {
    interrupted_ = false;
    deadline_ = Clock::now() + task_timeout;
    return !cancelled() && !window_expired();
  }
  void pause_for(Clock::duration elapsed) { deadline_ += elapsed; }
  bool window_expired() const { return window_ && Clock::now() >= *window_; }
  bool expired() const { return cancelled() || window_expired() || Clock::now() >= deadline_; }
  bool interrupt() {
    if (expired()) interrupted_ = true;
    return interrupted_;
  }
  bool interrupted() const { return interrupted_; }
  bool report_window_timeout() {
    if (window_reported_) return false;
    window_reported_ = true;
    return true;
  }

 private:
  std::shared_ptr<std::atomic<bool>> cancelled_;
  Clock::time_point deadline_ = Clock::now() + task_timeout;
  std::optional<Clock::time_point> window_;
  bool interrupted_ = false, window_reported_ = false;
};
}  // namespace webcli
