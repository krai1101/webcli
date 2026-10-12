#include "network/destination_policy.hpp"

#include <uv.h>

#include <algorithm>
#include <cstdint>

#include "core/errors.hpp"
#include "core/runtime_policy.hpp"

namespace webcli {
namespace {
bool private_v4(const unsigned char* address) {
  return address[0] == 0 || address[0] == 10 || address[0] == 127 ||
         (address[0] == 169 && address[1] == 254) ||
         (address[0] == 172 && address[1] >= 16 && address[1] <= 31) ||
         (address[0] == 192 && address[1] == 168);
}
bool private_address(const curl_sockaddr& address) {
  if (address.family == AF_INET) {
    const auto& ipv4 = reinterpret_cast<const sockaddr_in&>(address.addr);
    return private_v4(reinterpret_cast<const unsigned char*>(&ipv4.sin_addr));
  }
  if (address.family == AF_INET6) {
    const auto& ipv6 = reinterpret_cast<const sockaddr_in6&>(address.addr);
    const auto* bytes = reinterpret_cast<const unsigned char*>(&ipv6.sin6_addr);
    const bool zero_prefix = std::all_of(bytes, bytes + 15, [](unsigned char b) { return b == 0; });
    if ((zero_prefix && bytes[15] <= 1) || (bytes[0] == 0xfe && (bytes[1] & 0xc0) == 0x80) ||
        (bytes[0] & 0xfe) == 0xfc)
      return true;
    const bool mapped = std::all_of(bytes, bytes + 10, [](unsigned char b) { return b == 0; }) &&
                        bytes[10] == 0xff && bytes[11] == 0xff;
    return mapped && private_v4(bytes + 12);
  }
  return true;
}
}  // namespace

curl_socket_t DestinationPolicy::open_socket(void* opaque, curlsocktype purpose,
                                             curl_sockaddr* address) noexcept {
  auto& policy = *static_cast<DestinationPolicy*>(opaque);
  if (!address || purpose != CURLSOCKTYPE_IPCXN || private_address(*address)) {
    policy.blocked_ = true;
    return CURL_SOCKET_BAD;
  }
  return ::socket(address->family, address->socktype, address->protocol);
}
void DestinationPolicy::attach(CURL* handle) {
  if (!runtime_policy().block_private_networks) return;
  // A proxy would resolve the destination remotely, outside the socket filter.
  // Direct connections keep the opt-in policy enforceable on each actual IP.
  if (curl_easy_setopt(handle, CURLOPT_PROXY, "") != CURLE_OK ||
      curl_easy_setopt(handle, CURLOPT_OPENSOCKETFUNCTION, open_socket) != CURLE_OK ||
      curl_easy_setopt(handle, CURLOPT_OPENSOCKETDATA, this) != CURLE_OK)
    throw CliError("HTTP_INIT", "cannot enable private-network blocking");
}
}  // namespace webcli
