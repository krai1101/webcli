#pragma once

#include <cstdint>

#include <atomic>
#include <future>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>

#include "network/http_client.hpp"
#include "runtime/css/stylesheet_loader.hpp"

namespace webcli {
struct ScriptContext;
// Owner-thread stylesheet attachments share URL resources. Transport workers
// carry bytes only; DOM changes and element events run through the scheduler.
class StylesheetResources {
 public:
  ~StylesheetResources();
  void poll(ScriptContext&);
  bool pending() const;

 private:
  struct Resource {
    std::future<HttpResponse> transfer;
    std::shared_ptr<std::atomic<bool>> cancelled;
    std::shared_ptr<const std::string> source;
    std::string error;
    bool complete = false;
  };
  struct Attachment {
    std::string href;
    std::shared_ptr<Resource> resource;
    bool notified = false;
    bool stylesheet = true;
  };
  void scan(ScriptContext&);
  void queue_event(ScriptContext&, lxb_dom_node_t*, const Attachment&);
  std::map<std::string, std::shared_ptr<Resource>> resources_;
  std::unordered_map<lxb_dom_node_t*, Attachment> attachments_;
  std::size_t retained_bytes_ = 0, notifications_ = 0;
  std::uint64_t scanned_revision_ = 0;
};
}  // namespace webcli
