#pragma once
#include <string>

#include "network/referrer.hpp"

namespace webcli {
struct NavigationInitiator {
  std::string origin;
  NavigationReferrer referrer;
};
struct NavigationAction {
  std::string url;
  NavigationInitiator initiator;
  bool replace = false, reload = false;
};
}  // namespace webcli
