#pragma once
#include <map>
#include <string>
namespace webcli {
struct HttpRequest;
bool potentially_trustworthy_url(const std::string&);
// Redirect history may only reduce a request's site relationship. -2 means
// unclassified; -1 is a direct navigation, whose relationship remains "none".
std::map<std::string, std::string> fetch_metadata_headers(const HttpRequest&,
                                                          const std::string& target,
                                                          int& relationship);
}  // namespace webcli
