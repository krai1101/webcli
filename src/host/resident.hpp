#pragma once
#include "host/protocol.hpp"
namespace webcli {
Response send_request(const Request&);
int serve_browser();
}  // namespace webcli
