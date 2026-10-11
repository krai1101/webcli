#include "host/instance_worker.hpp"

#include <optional>
#include <sstream>

#include "cli/output/format.hpp"
#include "core/errors.hpp"
#include "runtime/page/browsing_instance.hpp"

namespace webcli {
InstanceWorker::InstanceWorker(std::shared_ptr<OriginStorage> storage)
    : thread_([this, storage = std::move(storage)] { run(storage); }) {}
InstanceWorker::~InstanceWorker() {
  stop();
  if (thread_.joinable()) thread_.join();
}
void InstanceWorker::stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
    cancelled_->store(true);
  }
  wake_.notify_one();
}
std::future<Response> InstanceWorker::enqueue(Request request) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (stopping_) throw CliError("INSTANCE_CLOSING", "browsing instance is closing");
  if (queue_.size() >= 64)
    throw CliError("COMMAND_QUEUE_FULL", "instance command queue is full", true);
  Work work{std::move(request), {}};
  auto future = work.reply.get_future();
  if (work.request.command == "close") {
    queue_.push_front(std::move(work));
    stopping_ = true;
    cancelled_->store(true);
  } else
    queue_.push_back(std::move(work));
  wake_.notify_one();
  return future;
}
void InstanceWorker::run(std::shared_ptr<OriginStorage> storage) {
  try {
    auto page = std::make_unique<BrowsingInstance>(std::move(storage), cancelled_);
    std::optional<Work> waiting;
    std::unique_ptr<WaitOperation> condition;
    while (true) {
      Work work;
      bool available = false;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        wake_.wait_for(lock, std::chrono::milliseconds(5),
                       [&] { return stopping_ || (!waiting && !queue_.empty()); });
        if (!queue_.empty() && (!waiting || queue_.front().request.command == "close")) {
          work = std::move(queue_.front());
          queue_.pop_front();
          available = true;
        } else if (stopping_) {
          if (waiting)
            waiting->reply.set_exception(std::make_exception_ptr(
                CliError("INSTANCE_CLOSED", "wait cancelled by instance shutdown")));
          break;
        }
      }
      if (waiting && available) {
        Response cancelled;
        cancelled.status = 1;
        std::ostringstream out;
        render_error(out, waiting->request.json, "WAIT_CANCELLED", "wait cancelled by close");
        (waiting->request.json ? cancelled.output : cancelled.error) = out.str();
        waiting->reply.set_value(std::move(cancelled));
        waiting.reset();
        condition.reset();
      }
      if (!available) {
        if (waiting) {
          Response completed;
          std::ostringstream out;
          bool done = false;
          try {
            if (const auto* observed = page->advance_wait(*condition)) {
              render(*observed, waiting->request, out);
              if (!waiting->request.json) page->mark_diagnostics_reported();
              completed.output = out.str();
              done = true;
            }
          } catch (const CliError& error) {
            completed.status = 1;
            done = true;
            render_error(out, waiting->request.json, error.code, error.what(), error.retryable);
            (waiting->request.json ? completed.output : completed.error) = out.str();
          } catch (const std::exception& error) {
            completed.status = 1;
            done = true;
            render_error(out, waiting->request.json, "INTERNAL_ERROR", error.what());
            (waiting->request.json ? completed.output : completed.error) = out.str();
          }
          if (done) {
            waiting->reply.set_value(std::move(completed));
            waiting.reset();
            condition.reset();
          }
        } else {
          try {
            page->tick_background();
          } catch (...) { /* page reports diagnostics */
          }
        }
        continue;
      }
      Response response;
      std::ostringstream out;
      try {
        if (work.request.command == "wait") {
          condition = page->begin_wait(engine_request(work.request));
          waiting = std::move(work);
          continue;
        } else if (work.request.command == "close") {
          page.reset();
          out << (work.request.json ? "{\"closed\":true}\n" : "Page closed.\n");
          stop();
        } else {
          render(page->execute(engine_request(work.request)), work.request, out);
          if (!work.request.json) page->mark_diagnostics_reported();
        }
        response.output = out.str();
      } catch (const CliError& error) {
        response.status = 1;
        render_error(out, work.request.json, error.code, error.what(), error.retryable);
        (work.request.json ? response.output : response.error) = out.str();
      } catch (const std::exception& error) {
        response.status = 1;
        render_error(out, work.request.json, "INTERNAL_ERROR", error.what());
        (work.request.json ? response.output : response.error) = out.str();
      }
      work.reply.set_value(std::move(response));
      if (!page) break;
    }
  } catch (...) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& work : queue_) work.reply.set_exception(std::current_exception());
    queue_.clear();
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
    cancelled_->store(true);
    for (auto& work : queue_)
      work.reply.set_exception(
          std::make_exception_ptr(CliError("INSTANCE_CLOSED", "instance closed")));
    queue_.clear();
  }
  finished_.store(true);
}
}  // namespace webcli
