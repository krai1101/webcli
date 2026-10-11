#pragma once
#include <string>

#include "network/referrer.hpp"

namespace webcli {
struct NavigationAction {
  std::string url;
  NavigationReferrer referrer;
  bool replace = false, reload = false;
};
}  // namespace webcli
