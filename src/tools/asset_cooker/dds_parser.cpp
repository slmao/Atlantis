#include "dds_parser.h"

#include <array>
#include <cstring>

namespace atlantis::asset_cooker {

namespace {

// DDS file layout (Microsoft DDS spec): 4-byte magic "DDS " followed by
// a 124-byte DDS_HEADER, optionally followed by a 20-byte DDS_HEADER_DXTEN
// when the pixel-format fourCC is "DX10". All fields little-endian u32.
constexpr std::size_t kMagicSize = 4;
constexpr std::size_t kHeaderSize = 124;
constexpr std::size_t kDx10HeaderSize = 20;
constexpr std::size_t kBaseHeaderBytes = kMagicSize + kHeaderSize;        // 128
constexpr std::size_t kDx10TotalHeaderBytes = kBaseHeaderBytes + kDx10HeaderSize;  // 148

// DDS_HEADER field offsets (relative to the header's own start).
constexpr std::size_t kHeaderHeightOffset = 8;
constexpr std::size_t kHeaderMipMapCountOffset = 24;  // valid only when DDSD_MIPMAPCOUNT is set
constexpr std::size_t kPixelFormatOffset = 72;        // DDPIXELFORMAT start (header-relative: 7 leading DWORDs + dwReserved1[11] = 28 + 44 = 72; empirically confirmed against NVIDIA RTXDI-Assets' own bistro DDS files)

// DDPIXELFORMAT field offsets (relative to its own start).
constexpr std::size_t kPfFourCCOffset = 8;

// DDS_HEADER_DXTEN field offsets (relative to its own start).
constexpr std::size_t kDx10FormatOffset = 0;
constexpr std::size_t kDx10ResourceDimensionOffset = 4;
constexpr std::size_t kDx10ArraySizeOffset = 12;

constexpr std::uint32_t kDDSDMipmapCountFlag = 0x20000;

constexpr std::uint32_t kDxgiFormatBc7Typeless = 98;
constexpr std::uint32_t kDxgiFormatBc7Unorm = 99;
constexpr std::uint32_t kDxgiFormatBc7UnormSrgb = 100;
// Plan 0037 Milestone 2's census gate correction (human-ruled
// 2026-09-19): 119 of Bistro's 343 referenced DDS files (every _ddna
// normal/data map) are DXGI 98, BC7_TYPELESS -- Spec 0038's survey had
// recorded them as BC7_UNORM. A typeless format carries no view
// semantics by definition; the consumer chooses. Atlantis consumes them
// as linear data (Bc7Unorm), the normal-map convention that survey's
// own classification intended.

constexpr std::uint32_t kD3d10ResourceDimensionTexture2D = 3;

[[nodiscard]] std::uint32_t readU32LE(const std::uint8_t* bytes) noexcept {
  return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8U) |
         (static_cast<std::uint32_t>(bytes[2]) << 16U) | (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

[[nodiscard]] std::uint64_t blockCountFor(std::uint32_t dimension) noexcept {
  return (static_cast<std::uint64_t>(dimension) + 3ULL) / 4ULL;
}

}  // namespace

atlantis::Result<DdsBc7Image, DdsParseError> parseDdsBc7(const std::uint8_t* bytes,
                                                          std::size_t byteCount) noexcept {
  using ResultT = atlantis::Result<DdsBc7Image, DdsParseError>;

  if (byteCount < kBaseHeaderBytes) return ResultT::Err(DdsParseError::Truncated);

  static constexpr std::array<char, 4> kMagic = {'D', 'D', 'S', ' '};
  if (std::memcmp(bytes, kMagic.data(), kMagicSize) != 0) return ResultT::Err(DdsParseError::MalformedHeader);

  const std::uint8_t* header = bytes + kMagicSize;
  if (readU32LE(header) != kHeaderSize) return ResultT::Err(DdsParseError::MalformedHeader);

  const std::uint32_t height = readU32LE(header + kHeaderHeightOffset);
  const std::uint32_t width = readU32LE(header + kHeaderHeightOffset + 4);
  if (width == 0 || height == 0) return ResultT::Err(DdsParseError::MalformedHeader);

  const std::uint8_t* pixelFormat = header + kPixelFormatOffset;
  const std::uint32_t pfSize = readU32LE(pixelFormat);
  if (pfSize != 32) return ResultT::Err(DdsParseError::MalformedHeader);

  const std::uint32_t fourCC = readU32LE(pixelFormat + kPfFourCCOffset);

  bool srgb = false;
  std::size_t dataOffset = 0;
  if (fourCC == static_cast<std::uint32_t>(('D' << 0) | ('X' << 8) | ('1' << 16) | ('0' << 24))) {
    if (byteCount < kDx10TotalHeaderBytes) return ResultT::Err(DdsParseError::Truncated);
    const std::uint8_t* dx10 = bytes + kBaseHeaderBytes;
    const std::uint32_t dxgiFormat = readU32LE(dx10 + kDx10FormatOffset);
    const std::uint32_t resourceDimension = readU32LE(dx10 + kDx10ResourceDimensionOffset);
    const std::uint32_t arraySize = readU32LE(dx10 + kDx10ArraySizeOffset);
    if (resourceDimension != kD3d10ResourceDimensionTexture2D || arraySize != 1) {
      return ResultT::Err(DdsParseError::MalformedHeader);
    }
    if (dxgiFormat == kDxgiFormatBc7Unorm || dxgiFormat == kDxgiFormatBc7Typeless) {
      srgb = false;  // TYPELESS consumed as linear -- see the constants above.
    } else if (dxgiFormat == kDxgiFormatBc7UnormSrgb) {
      srgb = true;
    } else {
      return ResultT::Err(DdsParseError::UnsupportedFormat);
    }
    dataOffset = kDx10TotalHeaderBytes;
  } else {
    // Legacy (non-DX10) DDS: BC7 has no sRGB distinction in the legacy
    // fourCC form, so both accepted spellings decode as linear.
    if (fourCC == static_cast<std::uint32_t>(('B' << 0) | ('C' << 8) | ('7' << 16))) {
      srgb = false;
    } else {
      return ResultT::Err(DdsParseError::UnsupportedFormat);
    }
    dataOffset = kBaseHeaderBytes;
  }

  if (width % 4 != 0 || height % 4 != 0) return ResultT::Err(DdsParseError::NonAlignedDimensions);

  const std::uint64_t baseMipBytes = blockCountFor(width) * blockCountFor(height) * 16ULL;
  const std::uint64_t availableFromData = static_cast<std::uint64_t>(byteCount) - dataOffset;
  if (availableFromData < baseMipBytes) return ResultT::Err(DdsParseError::Truncated);

  // Mip-count sanity only: a declared count beyond what the buffer could
  // hold is a malformed file, but extra mips beyond the base are simply
  // not returned (the artifact contract is base-mip-only this round) --
  // no validation of their contents.
  if (const std::uint32_t flags = readU32LE(header + 4); (flags & kDDSDMipmapCountFlag) != 0) {
    const std::uint32_t mipCount = readU32LE(header + kHeaderMipMapCountOffset);
    if (mipCount == 0) return ResultT::Err(DdsParseError::MalformedHeader);
  }

  DdsBc7Image image;
  image.width = width;
  image.height = height;
  image.srgb = srgb;
  image.baseMipBlockBytes.resize(static_cast<std::size_t>(baseMipBytes));
  std::memcpy(image.baseMipBlockBytes.data(), bytes + dataOffset, static_cast<std::size_t>(baseMipBytes));
  return ResultT::Ok(std::move(image));
}

}  // namespace atlantis::asset_cooker
