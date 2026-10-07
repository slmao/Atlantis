#pragma once

#include <atlantis/connection/runtime_control.h>
#include <atlantis/runtime/scene_extraction.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

// Plan 0055 P8 (ADR-0106 D5; Spec 0055 ruling Q2, C1 + C2): capture of the
// frame just run, between frames. Private to atlantis_runtime_host: the
// public surface is RuntimeApplication::captureFrameData() (and, for the
// image, captureImage()), whose definitions live in frame_capture.cpp.
namespace atlantis::runtime::detail {

// The frame data a client sees: the lighting block exactly as written to the
// camera uniform (its counted lights only), the camera matrices, and the
// draw-item count.
[[nodiscard]] atlantis::connection::FrameData toFrameData(const FrameLightingData& lighting,
                                                          const CameraMatrices& camera, std::size_t drawItemCount);

// Writes `width` x `height` 8-bit pixels as an RGBA PNG at `path`. `pixels`
// is tightly packed, four bytes per pixel, in BGRA order when `bgra` (the
// swapchain's Bgra8 formats) and RGBA otherwise. False if the file could not
// be written.
[[nodiscard]] bool writePng(const std::string& path, std::uint32_t width, std::uint32_t height,
                            std::span<const std::uint8_t> pixels, bool bgra);

}  // namespace atlantis::runtime::detail
