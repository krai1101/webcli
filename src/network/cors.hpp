#pragma once
#include "network/http_client.hpp"

namespace webcli {
void prepare_fetch(HttpRequest&);
std::optional<HttpRequest> cors_preflight(const HttpRequest&, const std::string& target,
                                          const std::string& method,
                                          const std::map<std::string, std::string>& headers);
void check_cors(const HttpResponse&, const HttpRequest&);
void check_preflight(const HttpResponse&, const HttpRequest&, const HttpRequest& preflight);
void filter_fetch_response(HttpResponse&, const HttpRequest&, bool crossed_origin);
}  // namespace webcli
