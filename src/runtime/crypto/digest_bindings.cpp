#include "runtime/crypto/digest_bindings.hpp"

#include <openssl/evp.h>

#include <array>
#include <memory>
#include <string>

#include "runtime/engine/js_handles.hpp"
#include "runtime/engine/script_context.hpp"

namespace webcli {
namespace {
std::string digest_name(JSContext* context, JSValueConst value) {
  const auto name = js_string(context, value);
  if (name == "SHA-1") return "SHA1";
  if (name == "SHA-256") return "SHA256";
  if (name == "SHA-384") return "SHA384";
  if (name == "SHA-512") return "SHA512";
  return {};
}
const std::uint8_t* buffer(JSContext* context, JSValueConst value, std::size_t& length) {
  if (!JS_IsArrayBuffer(value)) return nullptr;
  const auto* data = JS_GetArrayBuffer(context, &length, value);
  return length <= kBufferedBodyBytes ? data : nullptr;
}
}  // namespace

JSValue js_crypto_digest(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  if (argc < 2) return JS_ThrowTypeError(context, "algorithm and data required");
  const auto name = digest_name(context, argv[0]);
  if (name.empty()) return JS_ThrowTypeError(context, "unsupported digest algorithm");
  std::size_t length = 0;
  const auto* data = buffer(context, argv[1], length);
  if (!data) return JS_ThrowTypeError(context, "attached byte buffer within quota required");
  const std::unique_ptr<EVP_MD, decltype(&EVP_MD_free)> algorithm(
      EVP_MD_fetch(nullptr, name.c_str(), nullptr), EVP_MD_free);
  if (!algorithm) return JS_ThrowInternalError(context, "digest provider unavailable");
  std::array<unsigned char, EVP_MAX_MD_SIZE> output{};
  unsigned int written = 0;
  if (EVP_Digest(data, length, output.data(), &written, algorithm.get(), nullptr) != 1)
    return JS_ThrowInternalError(context, "digest operation failed");
  return JS_NewArrayBufferCopy(context, output.data(), written);
}

}  // namespace webcli
