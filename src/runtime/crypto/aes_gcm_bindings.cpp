#include "runtime/crypto/aes_gcm_bindings.hpp"

#include <openssl/crypto.h>
#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <climits>
#include <memory>
#include <vector>

#include "core/resource_limits.hpp"

namespace webcli {
namespace {
struct Bytes {
  const unsigned char* data = nullptr;
  std::size_t size = 0;
};

bool read_buffer(JSContext* context, JSValueConst value, Bytes& bytes) {
  if (!JS_IsArrayBuffer(value)) return false;
  bytes.data = JS_GetArrayBuffer(context, &bytes.size, value);
  return bytes.data != nullptr && bytes.size <= kBufferedBodyBytes && bytes.size <= INT_MAX;
}

// In particular, never retain or publish unauthenticated decryption output.
struct Scratch {
  std::vector<unsigned char> bytes;
  ~Scratch() {
    if (!bytes.empty()) OPENSSL_cleanse(bytes.data(), bytes.size());
  }
};
}  // namespace

JSValue js_crypto_aes_gcm(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
  if (argc < 6) return JS_ThrowTypeError(context, "AES-GCM parameters required");
  Bytes key, iv, aad, input;
  int tag_bits = 0;
  const int decrypt = JS_ToBool(context, argv[5]);
  if (!read_buffer(context, argv[0], key) || !read_buffer(context, argv[1], iv) ||
      !read_buffer(context, argv[2], aad) || !read_buffer(context, argv[3], input) ||
      JS_ToInt32(context, &tag_bits, argv[4]) < 0 || decrypt < 0)
    return JS_ThrowTypeError(context, "attached byte buffers within quota required");
  if (iv.size == 0 || (tag_bits != 32 && tag_bits != 64 && tag_bits != 96 && tag_bits != 104 &&
                       tag_bits != 112 && tag_bits != 120 && tag_bits != 128))
    return JS_ThrowTypeError(context, "invalid AES-GCM IV or tag length");
  const EVP_CIPHER* cipher = key.size == 16   ? EVP_aes_128_gcm()
                             : key.size == 24 ? EVP_aes_192_gcm()
                             : key.size == 32 ? EVP_aes_256_gcm()
                                              : nullptr;
  if (!cipher) return JS_ThrowTypeError(context, "invalid AES key length");
  const std::size_t tag_bytes = static_cast<std::size_t>(tag_bits / 8);
  if (decrypt && input.size < tag_bytes)
    return JS_ThrowInternalError(context, "AES-GCM authentication failed");
  const std::size_t payload = input.size - (decrypt ? tag_bytes : 0);
  if (!decrypt && payload > kBufferedBodyBytes - tag_bytes)
    return JS_ThrowInternalError(context, "AES-GCM output exceeds byte quota");
  const std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> state(EVP_CIPHER_CTX_new(),
                                                                              EVP_CIPHER_CTX_free);
  if (!state) return JS_ThrowOutOfMemory(context);
  auto fail = [&]() { return JS_ThrowInternalError(context, "AES-GCM operation failed"); };
  if (EVP_CipherInit_ex(state.get(), cipher, nullptr, nullptr, nullptr, !decrypt) != 1 ||
      EVP_CIPHER_CTX_ctrl(state.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(iv.size),
                          nullptr) != 1 ||
      EVP_CipherInit_ex(state.get(), nullptr, nullptr, key.data, iv.data, !decrypt) != 1)
    return fail();
  int written = 0;
  if (aad.size &&
      EVP_CipherUpdate(state.get(), nullptr, &written, aad.data, static_cast<int>(aad.size)) != 1)
    return fail();
  Scratch output;
  output.bytes.resize(payload + EVP_MAX_BLOCK_LENGTH);
  if (EVP_CipherUpdate(state.get(), output.bytes.data(), &written, input.data,
                       static_cast<int>(payload)) != 1)
    return fail();
  std::array<unsigned char, 16> tag{};
  if (decrypt) {
    std::copy_n(input.data + payload, tag_bytes, tag.data());
    if (EVP_CIPHER_CTX_ctrl(state.get(), EVP_CTRL_GCM_SET_TAG, static_cast<int>(tag_bytes),
                            tag.data()) != 1)
      return fail();
  }
  int final_bytes = 0;
  if (EVP_CipherFinal_ex(state.get(), output.bytes.data() + written, &final_bytes) != 1)
    return JS_ThrowInternalError(context, "AES-GCM authentication failed");
  const auto total = static_cast<std::size_t>(written + final_bytes);
  if (!decrypt &&
      EVP_CIPHER_CTX_ctrl(state.get(), EVP_CTRL_GCM_GET_TAG, static_cast<int>(tag_bytes),
                          output.bytes.data() + total) != 1)
    return fail();
  return JS_NewArrayBufferCopy(context, output.bytes.data(), total + (decrypt ? 0 : tag_bytes));
}
}  // namespace webcli
