#pragma once

namespace webcli {
// Responsive page logic and CSS visibility share a fixed desktop environment.
// These dimensions do not allocate a rendering surface.
inline constexpr int kDesktopViewportWidth = 1920;
inline constexpr int kDesktopViewportHeight = 1080;
}  // namespace webcli
