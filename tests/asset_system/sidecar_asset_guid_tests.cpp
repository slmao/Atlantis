// Plan 0047 M3 (P8; ADR-0097 D3): every sidecar carries asset_guid as its
// second line. Each of the five parsers round-trips it and rejects a
// renamed, malformed or nil GUID line.

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/asset_metadata.h>
#include <atlantis/asset_system/environment_metadata.h>
#include <atlantis/asset_system/material_metadata.h>
#include <atlantis/asset_system/scene_metadata.h>
#include <atlantis/asset_system/texture_metadata.h>

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

using namespace atlantis::asset_system;

namespace {

constexpr std::string_view kGuid = "0123abcd-89ab-4def-8123-456789abcdef";

struct SidecarKind {
  const char* name;
  std::string validText;
  std::function<atlantis::Result<AssetGuid, MetadataParseError>(std::string_view)> parseGuid;
};

template <typename Metadata, typename Parse>
[[nodiscard]] std::function<atlantis::Result<AssetGuid, MetadataParseError>(std::string_view)> guidOf(Parse parse) {
  return [parse](std::string_view text) {
    using ResultT = atlantis::Result<AssetGuid, MetadataParseError>;
    const auto parsed = parse(text);
    if (parsed.isErr()) return ResultT::Err(parsed.error());
    return ResultT::Ok(parsed.value().assetGuid);
  };
}

[[nodiscard]] std::vector<SidecarKind> sidecarKinds() {
  const AssetGuid guid = parseAssetGuid(kGuid).value();
  AssetMetadata mesh;
  mesh.assetGuid = guid;
  mesh.assetId = assetKey(guid);
  TextureMetadata texture;
  texture.assetGuid = guid;
  MaterialMetadata material;
  material.assetGuid = guid;
  EnvironmentMetadata environment;
  environment.assetGuid = guid;
  SceneMetadata scene;
  scene.assetGuid = guid;
  return {
      {"mesh", serializeAssetMetadata(mesh), guidOf<AssetMetadata>(parseAssetMetadata)},
      {"texture", serializeTextureMetadata(texture), guidOf<TextureMetadata>(parseTextureMetadata)},
      {"material", serializeMaterialMetadata(material), guidOf<MaterialMetadata>(parseMaterialMetadata)},
      {"environment", serializeEnvironmentMetadata(environment), guidOf<EnvironmentMetadata>(parseEnvironmentMetadata)},
      {"scene", serializeSceneMetadata(scene), guidOf<SceneMetadata>(parseSceneMetadata)},
  };
}

[[nodiscard]] std::string replaced(std::string text, std::string_view from, std::string_view to) {
  const auto at = text.find(from);
  REQUIRE(at != std::string::npos);
  return text.replace(at, from.size(), to);
}

}  // namespace

TEST_CASE("Every sidecar writes asset_guid as its second line and reads it back", "[asset_system][asset_guid]") {
  for (const SidecarKind& kind : sidecarKinds()) {
    INFO(kind.name);
    const std::size_t secondLine = kind.validText.find('\n') + 1;
    CHECK(kind.validText.substr(secondLine, 12 + kGuid.size()) == "asset_guid: " + std::string(kGuid));
    const auto parsed = kind.parseGuid(kind.validText);
    REQUIRE(parsed.isOk());
    CHECK(toString(parsed.value()) == kGuid);
  }
}

TEST_CASE("Every sidecar rejects a renamed, malformed or nil asset_guid line", "[asset_system][asset_guid]") {
  for (const SidecarKind& kind : sidecarKinds()) {
    INFO(kind.name);
    CHECK(kind.parseGuid(replaced(kind.validText, "asset_guid: ", "asset_uuid: ")).error() ==
          MetadataParseError::FieldNameMismatch);
    CHECK(kind.parseGuid(replaced(kind.validText, kGuid, "0123ABCD-89ab-4def-8123-456789abcdef")).error() ==
          MetadataParseError::MalformedValue);
    CHECK(kind.parseGuid(replaced(kind.validText, kGuid, "00000000-0000-0000-0000-000000000000")).error() ==
          MetadataParseError::MalformedValue);
  }
}
