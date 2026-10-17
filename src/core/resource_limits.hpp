#pragma once
#include <cstddef>

namespace webcli {
// Central policy for document runtime, transport and observation resources.
// Network defaults mirror the local Lightpanda reference configuration.
inline constexpr std::size_t kMaxBodyBytes = 1024 * 1024 * 1024;
inline constexpr long kHttpRequestTimeoutMs = 15000;
inline constexpr long kHttpConnectTimeoutMs = 8000;
inline constexpr std::size_t kMaxTextChars = 256 * 1024;
inline constexpr std::size_t kMaxLinks = 1000;
inline constexpr std::size_t kMaxButtons = 1000;
inline constexpr std::size_t kMaxForms = 100;
inline constexpr std::size_t kMaxFormFields = 1000;
// Scripts have per-response, metadata, concurrency and time bounds,
// without a lifetime request-count or cumulative source-byte quota.
inline constexpr std::size_t kMaxDocumentScripts = 1024;
inline constexpr std::size_t kMaxScriptBytes = kMaxBodyBytes;
inline constexpr long kScriptRequestTimeoutMs = kHttpRequestTimeoutMs;
inline constexpr long kScriptLoadTimeoutMs = 60000;
// Allows document/CSS acquisition and cleanup around the script load deadline.
inline constexpr long kBrowserCommandTimeoutMs = 150000;
// Navigation waits for the load lifecycle, matching agent-browser's default.
inline constexpr long kNavigationLoadTimeoutMs = 25000;
// Lightpanda and agent-browser use a 500 ms network-idle window.
inline constexpr long kNetworkIdleWindowMs = 500;
inline constexpr std::size_t kMaxScriptErrors = 30;

// Framework commit traversal still overflows at 2 MiB with the correct DOM
// hierarchy. Reserve half the host's 8 MiB stack for QuickJS interpreter frames,
// leaving the other half for native bindings. This is not a V8 sizing equivalent.
inline constexpr std::size_t kJsStackBytes = 4 * 1024 * 1024;
inline constexpr std::size_t kBufferedBodyBytes = kMaxBodyBytes;
inline constexpr std::size_t kFetchBodyBytes = 1024 * 1024;
inline constexpr std::size_t kFetchHeaderBytes = 64 * 1024;
inline constexpr std::size_t kResourceQueueSize = 256;
inline constexpr std::size_t kResourceConcurrency = 40;
inline constexpr unsigned kFetchRequestCount = 200;
inline constexpr std::size_t kActiveTimers = 4096;
inline constexpr long kTimerDelayMs = 2147483647;
inline constexpr long kJsTaskSeconds = 30;
inline constexpr long kJsOperationSeconds = 60;
inline constexpr std::size_t kDomAllocationBytes = 8 * 1024 * 1024;
// Counts nodes and attributes, including detached allocations. The original
// 50,000 ceiling rejected ordinary large API reference pages. Keep a finite
// allocation/registry ceiling; this is WebCLI policy, not a Lightpanda default.
inline constexpr std::size_t kDomNodes = 250000;
// CSS shares the transport concurrency ceiling, with separate retained data limits.
inline constexpr std::size_t kStylesheetRequests = 64;
inline constexpr std::size_t kStylesheetBodyBytes = 2 * 1024 * 1024;
inline constexpr std::size_t kVisibilityStylesheetBytes = 2 * 1024 * 1024;
inline constexpr long kStylesheetLoadTimeoutMs = kHttpRequestTimeoutMs;
// Markup insertion uses the document allocation ceiling. A smaller independent
// fragment cap rejects valid application shells before the DOM budget is used.
inline constexpr std::size_t kDomFragmentBytes = kDomAllocationBytes;
inline constexpr std::size_t kBlobUrlCount = 256;
inline constexpr std::size_t kBlobUrlBytes = 16 * 1024 * 1024;
}  // namespace webcli
