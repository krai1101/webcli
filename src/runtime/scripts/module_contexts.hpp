#pragma once
#include <map>
#include <string>

#include "network/referrer.hpp"

namespace webcli {
inline std::string classic_script_id(const std::string& url, int node_id) {
  return url + "#webcli-classic-" + std::to_string(node_id);
}
struct ModuleContext {
  std::string base_url;
  NavigationReferrer request;
  bool module = true;
};
// Metadata belongs to the document owner, never to a transport thread.
class ModuleContexts {
 public:
  const ModuleContext* find(const std::string&) const;
  void remember(const std::string& id, std::string base, NavigationReferrer, bool module = true);
  void received(const std::string& id, const std::string& final_url, const std::string& policy);
  void dispatched(const std::string& id);

 private:
  std::map<std::string, ModuleContext> entries_;
  std::size_t bytes_ = 0;
};
}  // namespace webcli
