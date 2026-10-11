#pragma once

#include <string>

#include "runtime/navigation/history_archive.hpp"

namespace webcli {
struct HistoryAction {
  enum class Kind { Push, Replace, Traverse };
  Kind kind;
  std::string url;
  HistoryStatePtr state;
  int delta = 0;
};
}  // namespace webcli
