#include <atlantis/connection/text.h>

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/cook_scene.h>
#include <atlantis/asset_system/decode_scene.h>
#include <atlantis/world/access/runtime_world_access.h>
#include <atlantis/world/ecs/world_components.h>
#include <atlantis/world/scene_instantiation.h>
#include <atlantis/world/world_schema.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

// Plan 0054 M3 (Spec 0054 R6; ruling Q2: T2 + F1 + the value table + J-i;
// ADR-0105 D5): the client text forms -- unique short type names, nested
// field paths, one value text per field kind, every text error, and the
// round-trip of every leaf value; validity stays the boundary's refusal.

namespace {

constexpr std::string_view kTestTag = "connection_text_tests";

namespace fs = std::filesystem;
namespace access = atlantis::world::access;
namespace ecs = atlantis::world::ecs;
using atlantis::asset_system::EntityGuid;
using atlantis::asset_system::ValidatedSceneData;

const std::string gProcessTag = std::to_string(std::random_device{}());
std::atomic<int> gScratchCounter{0};

[[nodiscard]] ValidatedSceneData cookAndDecodeScene(const std::string& sourceText) {
  const fs::path dir = fs::temp_directory_path() / ("atlantis_" + std::string(kTestTag)) /
                        ("fixture_" + gProcessTag + "_" + std::to_string(gScratchCounter.fetch_add(1)));
  fs::create_directories(dir);
  {
    std::ofstream out(dir / "scene.scene.txt", std::ios::binary | std::ios::trunc);
    out << sourceText;
  }
  const auto guid = atlantis::asset_system::deriveAssetGuid(
      atlantis::asset_system::parseAssetGuid("00520052-0052-4052-8052-005200520052").value(), "scene");
  REQUIRE(atlantis::asset_system::cookScene((dir / "scene.scene.txt").string(), guid,
                                            (dir / "scene.ascene").string(), (dir / "scene.ascene.meta.txt").string())
              .isOk());
  auto decoded =
      atlantis::asset_system::decodeScene((dir / "scene.ascene").string(), (dir / "scene.ascene.meta.txt").string());
  REQUIRE(decoded.isOk());
  std::error_code ec;
  fs::remove_all(dir, ec);
  return decoded.value();
}

// One node per component kind: a material renderable, a Directional and a
// Point light, the active camera, and a bare node.
constexpr const char* kSceneSource =
    "atlantis_scene_source_version: 7\n"
    "node_count: 5\n"
    "active_camera: 4\n"
    "node: node_id=1 guid=52052052-0001-4052-8052-000000000001 parent=none position=1.0 2.0 3.0 "
    "rotation=0.1 0.2 0.3 scale=1.0 2.0 1.0 mesh=52052052-00aa-4052-8052-0000000000aa "
    "material=52052052-00bb-4052-8052-0000000000bb\n"
    "node: node_id=2 guid=52052052-0002-4052-8052-000000000002 parent=none position=0.0 5.0 0.0 "
    "rotation=0.5 -0.6 0.0 scale=1.0 1.0 1.0 light=directional color=0.6 0.7 1.0 intensity=1.2\n"
    "node: node_id=3 guid=52052052-0003-4052-8052-000000000003 parent=1 position=0.8 0.3 0.5 "
    "rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 light=point color=1.0 0.6 0.3 intensity=3.0 range=2.5\n"
    "node: node_id=4 guid=52052052-0004-4052-8052-000000000004 parent=none position=0.0 2.2 7.0 "
    "rotation=-0.3054 0.0 0.0 scale=1.0 1.0 1.0 camera_fov_y=1.0472 camera_near_z=0.1 camera_far_z=100.0\n"
    "node: node_id=5 guid=52052052-0005-4052-8052-000000000005 parent=none position=0.0 0.0 0.0 "
    "rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0\n";

[[maybe_unused]] constexpr std::size_t kRenderableNode = 0;
[[maybe_unused]] constexpr std::size_t kDirectionalNode = 1;
[[maybe_unused]] constexpr std::size_t kPointNode = 2;
[[maybe_unused]] constexpr std::size_t kCameraNode = 3;
[[maybe_unused]] constexpr std::size_t kBareNode = 4;

[[maybe_unused]] [[nodiscard]] EntityGuid newGuid(std::uint32_t n) {
  EntityGuid guid;
  guid.bytes[0] = std::byte{0x52};
  guid.bytes[12] = static_cast<std::byte>((n >> 24) & 0xff);
  guid.bytes[13] = static_cast<std::byte>((n >> 16) & 0xff);
  guid.bytes[14] = static_cast<std::byte>((n >> 8) & 0xff);
  guid.bytes[15] = static_cast<std::byte>(n & 0xff);
  return guid;
}

template <typename T>
[[nodiscard]] atlantis::schema::TypeId typeOf() {
  return ecs::componentTypeId<T>();
}

[[maybe_unused]] [[nodiscard]] atlantis::schema::FieldId field(std::string_view owner, std::string_view name) {
  return atlantis::schema::fieldId(owner, name);
}

[[maybe_unused]] [[nodiscard]] access::AccessError refusal(access::RuntimeWorldAccess& boundary, access::Command command) {
  const access::CommandTicket ticket = boundary.submit(std::move(command));
  const auto report = boundary.applyPending();
  REQUIRE(report.failures.size() == 1);
  CHECK(report.failures[0].ticket == ticket);
  CHECK(report.applied == 0);
  return report.failures[0].error;
}

[[maybe_unused]] [[nodiscard]] access::SetProperty set(const EntityGuid& entity, atlantis::schema::TypeId component,
                                      atlantis::schema::FieldId fieldId, access::PropertyValue value) {
  return access::SetProperty{{entity, component, fieldId}, std::move(value)};
}

// A Point light with a WorldMatrix -- one extraction counts -- built through
// the boundary in the order Correction J1 allows.
[[maybe_unused]] void addPointLight(access::RuntimeWorldAccess& boundary, const EntityGuid& g) {
  using namespace atlantis::world;
  boundary.submit(access::CreateEntity{g});
  boundary.submit(access::AddComponent{g, typeOf<Light>()});
  boundary.submit(set(g, typeOf<Light>(), field("world::Light", "kind"), access::EnumValue{1}));
  boundary.submit(access::AddComponent{g, typeOf<WorldMatrix>()});
  const auto report = boundary.applyPending();
  REQUIRE(report.failures.empty());
}

}  // namespace

namespace {

namespace text = atlantis::connection::text;
using atlantis::connection::text::TextError;
using atlantis::schema::FieldDescriptor;
using atlantis::schema::TypeDescriptor;
using atlantis::schema::TypeId;

[[nodiscard]] std::span<const TypeDescriptor> worldTypes() { return atlantis::world::worldSchema(); }

[[nodiscard]] const FieldDescriptor& leafOf(std::string_view path) {
  const auto resolved = text::parsePath(worldTypes(), path);
  REQUIRE(resolved.isOk());
  return *resolved.value().leaf;
}

[[nodiscard]] std::vector<std::string_view> split(const std::string& joined) {
  std::vector<std::string_view> tokens;
  std::size_t start = 0;
  while (start < joined.size()) {
    const auto end = joined.find(' ', start);
    tokens.push_back(std::string_view(joined).substr(start, end == std::string::npos ? std::string::npos : end - start));
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return tokens;
}

[[nodiscard]] atlantis::Result<access::PropertyValue, TextError> parse(std::string_view path,
                                                                     std::vector<std::string_view> tokens) {
  return text::parseValue(worldTypes(), leafOf(path), tokens);
}

}  // namespace

TEST_CASE("text: every World schema type's short name is unique, and resolves like its qualified name",
          "[connection][text]") {
  std::vector<std::string_view> shortNames;
  for (const TypeDescriptor& type : worldTypes()) {
    shortNames.push_back(text::shortName(type.name));
    CHECK(text::findType(worldTypes(), text::shortName(type.name)) == &type);
    CHECK(text::findType(worldTypes(), type.name) == &type);
    CHECK(text::findType(worldTypes(), type.id) == &type);
  }
  std::sort(shortNames.begin(), shortNames.end());
  CHECK(std::adjacent_find(shortNames.begin(), shortNames.end()) == shortNames.end());
  CHECK(text::shortName("world::Light") == "Light");
  CHECK(text::findType(worldTypes(), "Nope") == nullptr);
}

TEST_CASE("text: property paths -- short and qualified types, nested fields, and their errors",
          "[connection][text]") {
  using atlantis::world::Camera;
  using atlantis::world::Light;
  const auto intensity = text::parsePath(worldTypes(), "Light.intensity");
  REQUIRE(intensity.isOk());
  CHECK(intensity.value().component == typeOf<Light>());
  CHECK(intensity.value().leaf->id == field("world::Light", "intensity"));
  const auto qualified = text::parsePath(worldTypes(), "world::Light.intensity");
  REQUIRE(qualified.isOk());
  CHECK(qualified.value().leaf == intensity.value().leaf);

  const auto density = text::parsePath(worldTypes(), "Camera.fog.density");
  REQUIRE(density.isOk());
  CHECK(density.value().component == typeOf<Camera>());  // addressed under its component (Spec 0049 J8)
  CHECK(density.value().leaf->id == field("world::CameraFog", "density"));

  CHECK(text::parsePath(worldTypes(), "Camera.fog").error() == TextError::NotALeaf);
  CHECK(text::parsePath(worldTypes(), "Light").error() == TextError::NotALeaf);
  CHECK(text::parsePath(worldTypes(), "Light.nope").error() == TextError::UnknownField);
  CHECK(text::parsePath(worldTypes(), "Light.intensity.x").error() == TextError::UnknownField);
  CHECK(text::parsePath(worldTypes(), "Light.").error() == TextError::UnknownField);
  CHECK(text::parsePath(worldTypes(), "Nope.x").error() == TextError::UnknownType);
  CHECK(text::parsePath(worldTypes(), "LightKind.x").error() == TextError::UnknownType);  // an enum has no fields

  // leavesOf() walks every World component to its 24 leaves (Spec 0052 R7).
  std::size_t leaves = 0;
  std::apply([&](auto... tag) { ((leaves += text::leavesOf(worldTypes(), typeOf<decltype(tag)>()).size()), ...); },
             ecs::WorldComponentTypes{});
  CHECK(leaves == 24);
  bool sawDensity = false;
  for (const auto& leaf : text::leavesOf(worldTypes(), typeOf<Camera>())) {
    if (leaf.path == "Camera.fog.density") sawDensity = leaf.leaf == density.value().leaf;
    const auto resolved = text::parsePath(worldTypes(), leaf.path);  // every canonical path parses back
    REQUIRE(resolved.isOk());
    CHECK(resolved.value().leaf == leaf.leaf);
  }
  CHECK(sawDensity);
}

TEST_CASE("text: one value text per field kind", "[connection][text]") {
  CHECK(parse("Light.intensity", {"6"}).value() == access::PropertyValue{6.0f});
  CHECK(parse("Light.intensity", {"-0.25"}).value() == access::PropertyValue{-0.25f});
  CHECK(parse("Light.color", {"0.2", "0.4", "0.9"}).value() ==
        access::PropertyValue{std::array<float, 3>{0.2f, 0.4f, 0.9f}});
  CHECK(parse("WorldMatrix.column3", {"1", "2", "3", "1"}).value() ==
        access::PropertyValue{std::array<float, 4>{1.0f, 2.0f, 3.0f, 1.0f}});
  CHECK(parse("Light.kind", {"Point"}).value() == access::PropertyValue{access::EnumValue{1}});
  CHECK(parse("Light.kind", {"Directional"}).value() == access::PropertyValue{access::EnumValue{0}});
  CHECK(parse("Renderable.meshAsset", {"42"}).value() == access::PropertyValue{std::uint64_t{42}});
  CHECK(parse("Renderable.materialAsset", {"none"}).value() == access::PropertyValue{access::Absent{}});
  CHECK(parse("Renderable.materialAsset", {"7"}).value() == access::PropertyValue{std::uint64_t{7}});

  CHECK(text::formatValue(worldTypes(), leafOf("Light.intensity"), 6.0f) == "6");
  CHECK(text::formatValue(worldTypes(), leafOf("Light.color"), std::array<float, 3>{1.0f, 0.5f, 0.25f}) ==
        "1 0.5 0.25");
  CHECK(text::formatValue(worldTypes(), leafOf("Light.kind"), access::EnumValue{1}) == "Point");
  CHECK(text::formatValue(worldTypes(), leafOf("Renderable.materialAsset"), access::Absent{}) == "none");

  // GUID kinds: no World field has one, so a described field stands in.
  FieldDescriptor guidField;
  guidField.kind = atlantis::schema::TypeKind::Primitive;
  guidField.primitive = atlantis::schema::PrimitiveKind::EntityGuid;
  const EntityGuid g = newGuid(5);
  const std::string gText = text::formatValue(worldTypes(), guidField, g);
  CHECK(gText == atlantis::asset_system::toString(g));
  CHECK(text::parseValue(worldTypes(), guidField, std::vector<std::string_view>{gText}).value() ==
        access::PropertyValue{g});
  guidField.primitive = atlantis::schema::PrimitiveKind::AssetGuid;
  CHECK(text::parseValue(worldTypes(), guidField, std::vector<std::string_view>{"not-a-guid"}).error() ==
        TextError::MalformedGuid);

  CHECK(text::parseEntity(text::formatEntity(g)).value() == g);
  CHECK(text::parseEntity("0B2C1DB2-not-a-guid").error() == TextError::MalformedGuid);
}

TEST_CASE("text: every text error", "[connection][text]") {
  CHECK(parse("Light.color", {"1", "2"}).error() == TextError::WrongValueCount);
  CHECK(parse("Light.intensity", {"1", "2"}).error() == TextError::WrongValueCount);
  CHECK(parse("Light.intensity", {"bright"}).error() == TextError::MalformedNumber);
  CHECK(parse("Light.intensity", {"1.5x"}).error() == TextError::MalformedNumber);
  CHECK(parse("Renderable.meshAsset", {"-1"}).error() == TextError::MalformedNumber);
  CHECK(parse("Renderable.meshAsset", {"1.5"}).error() == TextError::MalformedNumber);
  CHECK(parse("Renderable.meshAsset", {"none"}).error() == TextError::MalformedNumber);  // not Optional
  CHECK(parse("Light.kind", {"point"}).error() == TextError::UnknownEnumConstant);
  CHECK(parse("Light.kind", {"1"}).error() == TextError::UnknownEnumConstant);
  for (const TextError error : {TextError::MalformedGuid, TextError::UnknownType, TextError::UnknownField,
                                TextError::NotALeaf, TextError::WrongValueCount, TextError::MalformedNumber,
                                TextError::UnknownEnumConstant}) {
    CHECK_FALSE(text::toString(error).empty());
  }
}

TEST_CASE("text: every leaf of every entity's components formats, then parses back to an equal value",
          "[connection][text]") {
  const ValidatedSceneData scene = cookAndDecodeScene(kSceneSource);
  atlantis::world::BakedScene baked = atlantis::world::bakeScene(scene);
  access::RuntimeWorldAccess boundary(baked);
  std::size_t checked = 0;
  for (const EntityGuid& entity : boundary.listEntities()) {
    const std::vector<TypeId> components = boundary.listComponents(entity).value();
    for (const TypeId component : components) {
      for (const auto& leaf : text::leavesOf(worldTypes(), component)) {
        INFO(leaf.path);
        const access::PropertyValue value = boundary.getProperty({entity, component, leaf.leaf->id}).value();
        const std::string formatted = text::formatValue(worldTypes(), *leaf.leaf, value);
        const auto parsed = text::parseValue(worldTypes(), *leaf.leaf, split(formatted));
        REQUIRE(parsed.isOk());
        CHECK(parsed.value() == value);
        ++checked;
      }
    }
  }
  CHECK(checked > 24);  // more than one entity's worth of leaves
}

TEST_CASE("text: nan parses; the boundary refuses it (one rule source)", "[connection][text]") {
  using atlantis::world::Light;
  const auto value = parse("Light.intensity", {"nan"});
  REQUIRE(value.isOk());
  const ValidatedSceneData scene = cookAndDecodeScene(kSceneSource);
  atlantis::world::BakedScene baked = atlantis::world::bakeScene(scene);
  access::RuntimeWorldAccess boundary(baked);
  const access::CommandTicket ticket = boundary.submit(
      access::SetProperty{{scene.entityGuid(kPointNode), typeOf<Light>(), field("world::Light", "intensity")},
                          value.value()});
  const auto report = boundary.applyPending();
  REQUIRE(report.failures.size() == 1);
  CHECK(report.failures[0] == access::CommandFailure{ticket, access::AccessError::NonFiniteValue});
}
