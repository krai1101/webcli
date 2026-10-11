#include "runtime/navigation/history_archive.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace webcli {
HistoryState::HistoryState(std::shared_ptr<HistoryArchive> owner, int identifier,
                           std::vector<std::uint8_t> data, std::string url)
    : id(identifier), bytes(std::move(data)), source_url(std::move(url)), owner_(std::move(owner)) {
  owner_->bytes_ += bytes.size();
}
HistoryState::~HistoryState() { owner_->release(id, bytes.size()); }
HistoryStatePtr HistoryArchive::store(std::vector<std::uint8_t> bytes, std::string source_url,
                                      std::size_t replacement_credit) {
  if (replacement_credit > bytes_ || bytes_ - replacement_credit > kMaxBytes ||
      bytes.size() > kMaxBytes - (bytes_ - replacement_credit))
    throw std::length_error("serialized history state exceeds the instance's 8 MiB budget");
  if (next_ == std::numeric_limits<int>::max())
    throw std::length_error("history state identifiers exhausted");
  const auto id = next_++;
  HistoryStatePtr result(
      new HistoryState(shared_from_this(), id, std::move(bytes), std::move(source_url)));
  records_.emplace(id, result);
  return result;
}
HistoryStatePtr HistoryArchive::find(int id) const {
  const auto found = records_.find(id);
  return found == records_.end() ? nullptr : found->second.lock();
}
void HistoryArchive::release(int id, std::size_t bytes) {
  bytes_ -= bytes;
  records_.erase(id);
}
}  // namespace webcli
