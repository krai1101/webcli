#pragma once
#include <cstddef>
#include <memory>

namespace webcli {
// Authoritative native state. Event delivery is claimed before invoking page
// handlers so reentrant script execution cannot dispatch lifecycle events twice.
class DocumentLifecycle {
 public:
  class LoadBlocker {
   public:
    explicit LoadBlocker(DocumentLifecycle& owner) : owner_(owner) { ++owner_.pending_loads_; }
    ~LoadBlocker() { --owner_.pending_loads_; }
    LoadBlocker(const LoadBlocker&) = delete;
    LoadBlocker& operator=(const LoadBlocker&) = delete;

   private:
    DocumentLifecycle& owner_;
  };
  std::unique_ptr<LoadBlocker> block_load() { return std::make_unique<LoadBlocker>(*this); }
  enum class Readiness { Loading, Interactive, Complete };
  const char* ready_state() const {
    switch (readiness_) {
      case Readiness::Loading:
        return "loading";
      case Readiness::Interactive:
        return "interactive";
      case Readiness::Complete:
        return "complete";
    }
    return "loading";
  }
  bool finish_parser() {
    if (parser_finished_ || aborted_) return false;
    parser_finished_ = true;
    readiness_ = Readiness::Interactive;
    return true;
  }
  void finish_script_processing() { scripts_processed_ = true; }
  bool claim_content_loaded(bool deferred_pending) {
    if (aborted_ || content_loaded_ || !parser_finished_ || !scripts_processed_ || deferred_pending)
      return false;
    content_loaded_ = true;
    return true;
  }
  bool claim_load(bool load_pending) {
    if (aborted_ || load_dispatched_ || !content_loaded_ || load_pending || pending_loads_)
      return false;
    load_dispatched_ = true;
    readiness_ = Readiness::Complete;
    return true;
  }
  void abort() { aborted_ = true; }
  bool load_completed() const { return load_dispatched_; }
  bool aborted() const { return aborted_; }

 private:
  std::size_t pending_loads_ = 0;
  Readiness readiness_ = Readiness::Loading;
  bool parser_finished_ = false, scripts_processed_ = false;
  bool content_loaded_ = false, load_dispatched_ = false, aborted_ = false;
};
}  // namespace webcli
