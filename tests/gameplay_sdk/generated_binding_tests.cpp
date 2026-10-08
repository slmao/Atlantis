// Plan 0057 M1 (Spec 0057 R3, R5, R10; ADR-0111 D2): the committed generated
// headers compile (with their own id static_asserts), their bindings say
// exactly what the schema tables say, every component's leaves are
// connection::text's leaves in order, and value initialization means what
// the contract says: zeros, empty optionals, enum value 0 -- which is a
// declared constant only where zeroIsDeclared says so.

#include <atlantis/gameplay/generated/world.h>

#include "generated/synthetic.h"
#include "synthetic_schema.h"

#include <atlantis/connection/text.h>
#include <atlantis/world/world_schema.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

namespace {

namespace gp = atlantis::gameplay;
namespace w = atlantis::gameplay::world;
namespace syn = atlantis::gameplay::synthetic;
namespace schema = atlantis::schema;

template <class T>
[[nodiscard]] constexpr const gp::TypeBinding& bindingOf() {
  return gp::BindingOf<T>::table[gp::BindingOf<T>::index];
}

// Every binding equals its descriptor: kind, version, fields (name, id, kind,
// primitive, referenced type, Optional, Editable) and constants; and
// zeroIsDeclared is exactly "some constant is 0".
void checkTable(std::span<const gp::TypeBinding> bindings, std::span<const schema::TypeDescriptor> table) {
  REQUIRE(bindings.size() == table.size());
  for (std::size_t t = 0; t < table.size(); ++t) {
    const gp::TypeBinding& b = bindings[t];
    const schema::TypeDescriptor& d = table[t];
    INFO(d.name);
    CHECK(b.name == d.name);
    CHECK(b.id == d.id);
    CHECK(b.kind == d.kind);
    CHECK(b.schemaVersion == d.schemaVersion);
    REQUIRE(b.fields.size() == d.fields.size());
    for (std::size_t f = 0; f < d.fields.size(); ++f) {
      INFO(d.fields[f].name);
      CHECK(b.fields[f].name == d.fields[f].name);
      CHECK(b.fields[f].id == d.fields[f].id);
      CHECK(b.fields[f].kind == d.fields[f].kind);
      if (d.fields[f].kind == schema::TypeKind::Primitive) CHECK(b.fields[f].primitive == d.fields[f].primitive);
      CHECK(b.fields[f].type == d.fields[f].type);
      CHECK(b.fields[f].optional == schema::hasFlags(d.fields[f].flags, schema::FieldFlags::Optional));
      CHECK(b.fields[f].editable == schema::hasFlags(d.fields[f].flags, schema::FieldFlags::Editable));
    }
    REQUIRE(b.constants.size() == d.constants.size());
    bool zero = false;
    for (std::size_t c = 0; c < d.constants.size(); ++c) {
      CHECK(b.constants[c].name == d.constants[c].name);
      CHECK(b.constants[c].value == d.constants[c].value);
      zero = zero || d.constants[c].value == 0;
    }
    CHECK(b.zeroIsDeclared == zero);
  }
}

template <class C>
void checkLeaves(std::span<const schema::TypeDescriptor> table) {
  const auto leaves = atlantis::connection::text::leavesOf(table, bindingOf<C>().id);
  REQUIRE(leaves.size() == gp::Codec<C>::leaves.size());
  for (std::size_t i = 0; i < leaves.size(); ++i) {
    INFO(leaves[i].path);
    CHECK(gp::Codec<C>::leaves[i] == leaves[i].leaf->id);
  }
}

}  // namespace

TEST_CASE("the generated World bindings say exactly what worldSchema() says", "[gameplay_sdk][generated]") {
  checkTable(w::kTypeBindings, atlantis::world::worldSchema());
  checkLeaves<w::Transform>(atlantis::world::worldSchema());
  checkLeaves<w::Camera>(atlantis::world::worldSchema());
  checkLeaves<w::Light>(atlantis::world::worldSchema());
  checkLeaves<w::Renderable>(atlantis::world::worldSchema());
  checkLeaves<w::WorldMatrix>(atlantis::world::worldSchema());
  CHECK(w::fields::Light.intensity.path == "Light.intensity");
  CHECK(w::fields::Camera.fog.density.path == "Camera.fog.density");
  CHECK(w::fields::Camera.fog.density.component == schema::typeId("world::Camera"));
  CHECK(w::fields::Camera.fog.density.field == schema::fieldId("world::CameraFog", "density"));
}

TEST_CASE("the generated synthetic bindings say exactly what the synthetic schema says", "[gameplay_sdk][generated]") {
  checkTable(syn::kTypeBindings, atlantis::test::synthetic_schema::table());
  checkLeaves<syn::Probe>(atlantis::test::synthetic_schema::table());
  checkLeaves<syn::Locked>(atlantis::test::synthetic_schema::table());
}

TEST_CASE("handles: the C++ type is the leaf's kind, and a read-only leaf gets ReadOnlyField",
          "[gameplay_sdk][generated]") {
  STATIC_CHECK(std::is_same_v<std::remove_cvref_t<decltype(w::fields::Light.intensity)>, gp::Field<w::Light, float>>);
  STATIC_CHECK(std::is_same_v<std::remove_cvref_t<decltype(w::fields::Light.kind)>, gp::Field<w::Light, w::LightKind>>);
  STATIC_CHECK(std::is_same_v<std::remove_cvref_t<decltype(w::fields::Renderable.materialAsset)>,
                              gp::Field<w::Renderable, std::optional<std::uint64_t>>>);
  STATIC_CHECK(std::is_same_v<std::remove_cvref_t<decltype(w::fields::WorldMatrix.column3)>,
                              gp::Field<w::WorldMatrix, std::array<float, 4>>>);
  STATIC_CHECK(
      std::is_same_v<std::remove_cvref_t<decltype(syn::fields::Locked.fixed)>, gp::ReadOnlyField<syn::Locked, float>>);
  STATIC_CHECK(std::is_same_v<std::remove_cvref_t<decltype(syn::fields::Locked.free)>, gp::Field<syn::Locked, float>>);
  STATIC_CHECK(gp::Codec<w::Light>::allEditable);
  STATIC_CHECK(gp::Codec<syn::Probe>::allEditable);
  STATIC_CHECK_FALSE(gp::Codec<syn::Locked>::allEditable);
}

TEST_CASE("value initialization: zeros, empty optionals, enum value 0 -- declared only where zeroIsDeclared says (R10)",
          "[gameplay_sdk][generated]") {
  // Underlying 0, whatever the enum declares.
  STATIC_CHECK(static_cast<std::int64_t>(w::LightKind{}) == 0);
  STATIC_CHECK(static_cast<std::int64_t>(syn::Mode{}) == 0);
  STATIC_CHECK(static_cast<std::int64_t>(syn::Phase{}) == 0);
  // LightKind{} is Directional because Directional is 0 -- if that ever
  // changes, this fails rather than value-initialized Lights silently
  // becoming something else.
  STATIC_CHECK(w::LightKind{} == w::LightKind::Directional);
  STATIC_CHECK(bindingOf<w::LightKind>().zeroIsDeclared);
  // Mode declares no 0: a value-initialized Mode is no declared constant.
  STATIC_CHECK_FALSE(bindingOf<syn::Mode>().zeroIsDeclared);
  STATIC_CHECK(syn::Mode{} != syn::Mode::A);
  STATIC_CHECK(syn::Mode{} != syn::Mode::B);
  STATIC_CHECK(syn::Mode{} != syn::Mode::C);
  // Phase declares 0, but not first: value initialization is Zero, not First.
  STATIC_CHECK(bindingOf<syn::Phase>().zeroIsDeclared);
  STATIC_CHECK(syn::Phase{} == syn::Phase::Zero);
  STATIC_CHECK(syn::Phase{} != syn::Phase::First);
  // Exact values, never ordinals.
  STATIC_CHECK(static_cast<std::int64_t>(syn::Mode::B) == -2);
  STATIC_CHECK(static_cast<std::int64_t>(syn::Mode::C) == 7);

  const w::Light light{};
  CHECK(light.color == std::array<float, 3>{0.0f, 0.0f, 0.0f});
  CHECK(light.intensity == 0.0f);
  CHECK(light.range == 0.0f);
  const w::Renderable renderable{};
  CHECK_FALSE(renderable.materialAsset.has_value());
}
