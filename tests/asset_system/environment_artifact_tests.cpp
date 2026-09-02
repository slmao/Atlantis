#include <atlantis/asset_system/environment_artifact.h>

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <limits>

using namespace atlantis::asset_system;

namespace {

[[nodiscard]] EnvironmentAssetData makeData() {
  EnvironmentAssetData data;
  data.faceSize = 2;
  data.mipCount = 2;
  data.dfgWidth = 2;
  data.dfgHeight = 2;
  for (std::size_t i = 0; i < data.irradianceSh.size(); ++i) data.irradianceSh[i] = static_cast<float>(i) * 0.25F;
  data.specularRgba16Float.resize((2 * 2 + 1) * 6 * 4, 0x3C00U);
  data.dfgRg16Float.resize(2 * 2 * 2, 0x3800U);
  return data;
}

void writeU32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) {
  for (std::size_t i = 0; i < 4; ++i) bytes[offset + i] = static_cast<std::byte>((value >> (8U * i)) & 0xFFU);
}

}  // namespace

TEST_CASE("environment artifact round-trips exact CPU payloads", "[asset_system]") {
  const EnvironmentAssetData original = makeData();
  const AssetId assetId = 0x0102030405060708ULL;
  const auto bytes = encodeEnvironmentArtifact(assetId, original);
  const auto decoded = decodeEnvironmentArtifact(bytes);
  REQUIRE(decoded.isOk());
  CHECK(decoded.value().assetId == assetId);
  CHECK(decoded.value().data.faceSize == original.faceSize);
  CHECK(decoded.value().data.mipCount == original.mipCount);
  CHECK(decoded.value().data.dfgWidth == original.dfgWidth);
  CHECK(decoded.value().data.dfgHeight == original.dfgHeight);
  CHECK(decoded.value().data.irradianceSh == original.irradianceSh);
  CHECK(decoded.value().data.specularRgba16Float == original.specularRgba16Float);
  CHECK(decoded.value().data.dfgRg16Float == original.dfgRg16Float);
}

TEST_CASE("environment artifact pins its 60-byte little-endian header", "[asset_system]") {
  const EnvironmentAssetData data = makeData();
  const auto bytes = encodeEnvironmentArtifact(0x0102030405060708ULL, data);
  REQUIRE(bytes.size() == 460);
  const std::vector<std::byte> expectedHeader = {
      std::byte{0x41}, std::byte{0x54}, std::byte{0x4C}, std::byte{0x45}, std::byte{0x4E}, std::byte{0x56},
      std::byte{0x00}, std::byte{0x00}, std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
      std::byte{0x08}, std::byte{0x07}, std::byte{0x06}, std::byte{0x05}, std::byte{0x04}, std::byte{0x03},
      std::byte{0x02}, std::byte{0x01}, std::byte{0x02}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
      std::byte{0x02}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x02}, std::byte{0x00},
      std::byte{0x00}, std::byte{0x00}, std::byte{0x02}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
      std::byte{0x3C}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x90}, std::byte{0x00},
      std::byte{0x00}, std::byte{0x00}, std::byte{0xCC}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
      std::byte{0xF0}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0xBC}, std::byte{0x01},
      std::byte{0x00}, std::byte{0x00}, std::byte{0x10}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
  };
  CHECK(std::vector<std::byte>(bytes.begin(), bytes.begin() + kEnvironmentArtifactHeaderSizeBytes) == expectedHeader);
}

TEST_CASE("environment artifact rejects malformed structure", "[asset_system]") {
  const EnvironmentAssetData data = makeData();

  SECTION("truncated header") {
    CHECK(decodeEnvironmentArtifact(std::vector<std::byte>(8)).error() ==
          EnvironmentArtifactDecodeError::TruncatedHeader);
  }
  SECTION("bad magic") {
    auto bytes = encodeEnvironmentArtifact(1, data);
    bytes[0] = std::byte{0};
    CHECK(decodeEnvironmentArtifact(bytes).error() == EnvironmentArtifactDecodeError::BadMagic);
  }
  SECTION("unknown schema") {
    auto bytes = encodeEnvironmentArtifact(1, data);
    writeU32(bytes, 8, 2);
    CHECK(decodeEnvironmentArtifact(bytes).error() == EnvironmentArtifactDecodeError::UnsupportedSchemaVersion);
  }
  SECTION("non-power-of-two dimension") {
    auto bytes = encodeEnvironmentArtifact(1, data);
    writeU32(bytes, 20, 3);
    CHECK(decodeEnvironmentArtifact(bytes).error() == EnvironmentArtifactDecodeError::InvalidDimensions);
  }
  SECTION("wrong full-chain mip count") {
    auto bytes = encodeEnvironmentArtifact(1, data);
    writeU32(bytes, 24, 1);
    CHECK(decodeEnvironmentArtifact(bytes).error() == EnvironmentArtifactDecodeError::InvalidMipCount);
  }
  SECTION("non-contiguous offset") {
    auto bytes = encodeEnvironmentArtifact(1, data);
    writeU32(bytes, 44, 205);
    CHECK(decodeEnvironmentArtifact(bytes).error() == EnvironmentArtifactDecodeError::InconsistentPayloadLayout);
  }
  SECTION("total size mismatch") {
    auto bytes = encodeEnvironmentArtifact(1, data);
    bytes.pop_back();
    CHECK(decodeEnvironmentArtifact(bytes).error() == EnvironmentArtifactDecodeError::InconsistentPayloadLayout);
  }
}

TEST_CASE("environment artifact rejects non-finite float32 and binary16 payloads", "[asset_system]") {
  const EnvironmentAssetData data = makeData();
  SECTION("SH NaN") {
    auto bytes = encodeEnvironmentArtifact(1, data);
    writeU32(bytes, kEnvironmentArtifactHeaderSizeBytes, std::bit_cast<std::uint32_t>(std::numeric_limits<float>::quiet_NaN()));
    CHECK(decodeEnvironmentArtifact(bytes).error() == EnvironmentArtifactDecodeError::NonFiniteValue);
  }
  SECTION("specular infinity") {
    auto bytes = encodeEnvironmentArtifact(1, data);
    const std::size_t specularOffset = kEnvironmentArtifactHeaderSizeBytes + kEnvironmentShPayloadSizeBytes;
    bytes[specularOffset] = std::byte{0x00};
    bytes[specularOffset + 1] = std::byte{0x7C};
    CHECK(decodeEnvironmentArtifact(bytes).error() == EnvironmentArtifactDecodeError::NonFiniteValue);
  }
  SECTION("DFG NaN") {
    auto bytes = encodeEnvironmentArtifact(1, data);
    const std::size_t dfgOffset = kEnvironmentArtifactHeaderSizeBytes + kEnvironmentShPayloadSizeBytes + 240;
    bytes[dfgOffset] = std::byte{0x01};
    bytes[dfgOffset + 1] = std::byte{0x7E};
    CHECK(decodeEnvironmentArtifact(bytes).error() == EnvironmentArtifactDecodeError::NonFiniteValue);
  }
}
