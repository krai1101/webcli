#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace webcli {
class HistoryArchive;
struct HistoryState {
  HistoryState(std::shared_ptr<HistoryArchive>, int, std::vector<std::uint8_t>, std::string);
  ~HistoryState();
  HistoryState(const HistoryState&) = delete;
  HistoryState& operator=(const HistoryState&) = delete;
  const int id;
  const std::vector<std::uint8_t> bytes;
  const std::string source_url;

 private:
  std::shared_ptr<HistoryArchive> owner_;
};
using HistoryStatePtr = std::shared_ptr<const HistoryState>;

// Instance-owned serialized state. Entries/actions hold payloads; the lookup is
// weak, so removing the last owner releases both bytes and its lookup slot.
class HistoryArchive : public std::enable_shared_from_this<HistoryArchive> {
 public:
  static constexpr std::size_t kMaxBytes = 8 * 1024 * 1024;
  HistoryStatePtr store(std::vector<std::uint8_t>, std::string source_url,
                        std::size_t replacement_credit = 0);
  HistoryStatePtr find(int) const;

 private:
  friend struct HistoryState;
  void release(int, std::size_t);
  std::unordered_map<int, std::weak_ptr<const HistoryState>> records_;
  std::size_t bytes_ = 0;
  int next_ = 1;
};
struct HistoryContext {
  std::shared_ptr<HistoryArchive> archive;
  std::size_t length = 1, offset = 0;
  int state_id = 0;
};
}  // namespace webcli
