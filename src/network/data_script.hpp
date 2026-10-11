#pragma once
#include <string>
namespace webcli {
bool is_data_script_url(const std::string&);
std::string decode_data_script(const std::string&);
}  // namespace webcli
