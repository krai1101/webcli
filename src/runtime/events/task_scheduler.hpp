#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>

namespace webcli {
// Owner-thread scheduler. Due time plus sequence gives deterministic FIFO
// ordering, including timers that become due during the same engine turn.
class TaskScheduler {
 public:
  using Clock = std::chrono::steady_clock;
  using TaskId = std::uint64_t;
  enum class Source { Timer, Resource, Script, Observer, Navigation };
  TaskId schedule(Source source, Clock::duration delay, std::function<void()> callback) {
    const auto id = ++sequence_;
    tasks_.emplace(Key{Clock::now() + delay, id}, Task{source, std::move(callback)});
    return id;
  }
  void cancel(TaskId id) {
    for (auto it = tasks_.begin(); it != tasks_.end(); ++it)
      if (it->first.second == id) {
        tasks_.erase(it);
        return;
      }
  }
  bool run_one() {
    if (paused_ || !has_ready()) return false;
    auto task = std::move(tasks_.begin()->second);
    tasks_.erase(tasks_.begin());
    ++generation_;
    task.callback();
    return true;
  }
  bool has_ready() const { return due_within(Clock::duration::zero()); }
  bool due_within(Clock::duration duration) const {
    return !paused_ && !tasks_.empty() && tasks_.begin()->first.first <= Clock::now() + duration;
  }
  std::optional<Clock::time_point> next_deadline() const {
    if (paused_ || tasks_.empty()) return std::nullopt;
    return tasks_.begin()->first.first;
  }
  void pause() {
    if (!paused_) paused_ = Clock::now();
  }
  void resume() {
    if (!paused_) return;
    const auto elapsed = Clock::now() - *paused_;
    decltype(tasks_) shifted;
    for (auto& entry : tasks_)
      shifted.emplace(Key{entry.first.first + elapsed, entry.first.second},
                      std::move(entry.second));
    tasks_ = std::move(shifted);
    paused_.reset();
  }
  void clear() { tasks_.clear(); }
  std::uint64_t generation() const { return generation_; }

 private:
  using Key = std::pair<Clock::time_point, TaskId>;
  struct Task {
    Source source;
    std::function<void()> callback;
  };
  std::map<Key, Task> tasks_;
  std::optional<Clock::time_point> paused_;
  TaskId sequence_ = 0;
  std::uint64_t generation_ = 0;
};
}  // namespace webcli
