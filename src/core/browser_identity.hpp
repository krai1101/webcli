#pragma once
#include <string>
namespace webcli {
// Keep HTTP and JavaScript identity consistent. Chrome-compatible identification
// is a website compatibility policy; execution remains in WebCLI's native engine.
inline constexpr char kBrowserBrand[] = "Google Chrome";
inline constexpr char kBrowserMajorVersion[] = "154";
inline constexpr char kBrowserVersion[] = "154.0.0.0";
#if defined(__aarch64__) || defined(__arm__) || defined(_M_ARM64) || defined(_M_ARM)
inline constexpr char kBrowserArchitecture[] = "arm";
#elif defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
inline constexpr char kBrowserArchitecture[] = "x86";
#else
inline constexpr char kBrowserArchitecture[] = "";
#endif
inline constexpr const char* kBrowserBitness = sizeof(void*) == 8 ? "64" : "32";
#if defined(_WIN32)
inline constexpr char kUserAgentPlatform[] = "Windows NT 10.0; Win64; x64";
#elif defined(__APPLE__)
inline constexpr char kUserAgentPlatform[] = "Macintosh; Intel Mac OS X 10_15_7";
#else
inline constexpr char kUserAgentPlatform[] = "X11; Linux x86_64";
#endif
inline const std::string kUserAgent =
    std::string("Mozilla/5.0 (") + kUserAgentPlatform +
    ") AppleWebKit/537.36 (KHTML, like Gecko) Chrome/" + kBrowserVersion + " Safari/537.36";
inline const std::string kClientHintBrand =
    std::string("\"") + kBrowserBrand + "\";v=\"" + kBrowserMajorVersion + "\"";
inline const std::string kClientHintFullVersion =
    std::string("\"") + kBrowserBrand + "\";v=\"" + kBrowserVersion + "\"";
}  // namespace webcli
