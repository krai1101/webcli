#pragma once
#include <cstdint>
#include <chrono>
#include <memory>
#include <map>
#include <vector>

#include "core/page.hpp"
#include "network/referrer.hpp"
#include "runtime/navigation/history_archive.hpp"
#include "runtime/page/engine_request.hpp"
#include "runtime/page/wait_operation.hpp"
#include "storage/profile_store.hpp"
namespace webcli {
class BrowsingInstance {
 public:
  explicit BrowsingInstance(
      std::shared_ptr<OriginStorage> local = std::make_shared<OriginStorage>(),
      std::shared_ptr<std::atomic<bool>> cancelled = {});
  ~BrowsingInstance();
  const Page& execute(const EngineRequest&);
  std::unique_ptr<WaitOperation> begin_wait(const EngineRequest&);
  const Page* advance_wait(WaitOperation&);
  void tick();
  void tick_background();
  void mark_diagnostics_reported();
#ifdef WEBCLI_UPSTREAM_TEST_DRIVER
  // Test-only DOM access; absent from the product executable and CLI protocol.
  std::string test_evaluate(const std::string& expression);
  void test_action(EngineRequest request);
#endif

 private:
  struct Document;
  struct Entry {
    std::shared_ptr<Document> document;
    std::string url;
    HistoryStatePtr state;
    std::uint64_t group = 0;
    std::shared_ptr<const NavigationReferrer> referrer;
  };
  StorageStore storage_;
  std::shared_ptr<std::atomic<bool>> cancelled_;
  std::shared_ptr<HistoryArchive> history_archive_ = std::make_shared<HistoryArchive>();
  std::uint64_t next_history_group_ = 0;
  std::map<char, std::size_t> reference_counts_;
  std::shared_ptr<Document> current_;
  std::vector<Entry> history_;
  std::size_t position_ = 0;
  void navigate(const std::string&, const std::string& method = "GET", const std::string& body = "",
                bool replace = false, bool is_form_submission = false,
                const NavigationReferrer* referrer = nullptr);
  void move_history(int);
  void trim_history();
  void transitions(unsigned depth = 0);
  void snapshot();
  const Page& publish();
  std::string resolve_reference(const std::string&) const;
  void wait_for_load(std::chrono::steady_clock::time_point deadline);
  void wait_after_click(std::chrono::steady_clock::time_point deadline);
  int find_node(const std::string&) const;
  int find_frame(const std::string&) const;
  void submit_form(int form, int button);
};
}  // namespace webcli
