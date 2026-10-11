#include "network/public_suffix.hpp"

#include <libpsl.h>

#include "core/errors.hpp"

namespace webcli {
bool public_suffix(const std::string& domain) {
  const auto* context = psl_builtin();
  if (!context) throw CliError("PSL_UNAVAILABLE", "libpsl has no built-in public suffix data");
  return psl_is_public_suffix(context, domain.c_str()) != 0;
}
}  // namespace webcli
