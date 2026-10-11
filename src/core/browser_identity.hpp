#pragma once
#include <string>
namespace webcli {
// Use the same honest engine identity for HTTP and navigator.userAgent. Claiming
// another browser's engine can select APIs and request checks we do not satisfy.
inline constexpr char kBrowserBrand[] = "WebCLI";
inline constexpr char kBrowserMajorVersion[] = "0";
inline constexpr char kBrowserVersion[] = "0.2";
inline const std::string kUserAgent = std::string(kBrowserBrand) + "/" + kBrowserVersion;
inline const std::string kClientHintBrand =
    std::string("\"") + kBrowserBrand + "\";v=\"" + kBrowserMajorVersion + "\"";
inline const std::string kClientHintFullVersion =
    std::string("\"") + kBrowserBrand + "\";v=\"" + kBrowserVersion + "\"";
}  // namespace webcli
