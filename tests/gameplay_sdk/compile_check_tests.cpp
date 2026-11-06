#include <atlantis/gameplay/assets.h>
// Plan 0057 M3 (Spec 0057 R3, R10; ADR-0111 D2, D3; J3): what the typed
// layer refuses at compile time, checked as requires-expressions -- a set of
// the wrong kind, a set through a ReadOnlyField, an add of a type with a
// read-only leaf, an integer for an enum. Each negative check has its
// positive twin beside it, so a false is attributable. The same three cases
// are also built as real compile failures (probes/gameplay_probe.cpp, J3).

#include <atlantis/gameplay/generated/world.h>
#include <atlantis/gameplay/transaction.h>
#include <atlantis/gameplay/world.h>

#include "generated/synthetic.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <string_view>
#include <type_traits>

TEST_CASE("gameplay assets: the facade borrows Connection and returns independent query values", "[gameplay_sdk][assets]") {
  namespace a = atlantis::connection::assets;
  class Endpoint final : public atlantis::connection::AssetConnection {
   public:
    atlantis::asset_system::AssetGuid guid = atlantis::asset_system::parseAssetGuid("00810081-0081-4081-8081-008100810081").value();
    atlantis::Result<std::vector<a::AssetSummary>, a::AssetError> listAssets() const override {
      return atlantis::Result<std::vector<a::AssetSummary>, a::AssetError>::Ok({{guid, a::AssetType::Mesh, "assets:mesh.txt"}});
    }
    atlantis::Result<a::AssetDetails, a::AssetError> getAsset(const atlantis::asset_system::AssetGuid& id) const override {
      using R = atlantis::Result<a::AssetDetails, a::AssetError>;
      return id == guid ? R::Ok({guid, a::AssetType::Mesh, "assets:mesh.txt", 7, std::nullopt, "test/1", {}})
                        : R::Err(a::AssetError::UnknownAsset);
    }
  } endpoint;
  atlantis::gameplay::Assets sdk(endpoint);
  auto list = sdk.listAssets(); REQUIRE(list.isOk());
  CHECK(sdk.getAsset(list.value()[0].guid).value().source == list.value()[0].source);
  list.value()[0].source = "client copy";
  CHECK(sdk.listAssets().value()[0].source == "assets:mesh.txt");
}

namespace {

namespace gp = atlantis::gameplay;
namespace w = atlantis::gameplay::world;
namespace syn = atlantis::gameplay::synthetic;

template <class Handle, class Value>
concept CanSet = requires(gp::World& world, const gp::EntityGuid& entity, const Handle& handle, Value value) {
  world.set(entity, handle, value);
};

template <class Handle, class Value>
concept CanSetInTransaction =
    requires(gp::Transaction& tx, const gp::EntityGuid& entity, const Handle& handle, Value value) {
      tx.set(entity, handle, value);
    };

template <class Handle>
concept CanGet = requires(const gp::World& world, const gp::EntityGuid& entity, const Handle& handle) {
  world.get(entity, handle);
};

template <class C>
concept CanAddValue = requires(gp::Transaction& tx, const gp::EntityGuid& entity, const C& value) {
  tx.add(entity, value);
};

template <class C>
concept CanAddBare = requires(gp::Transaction& tx, const gp::EntityGuid& entity) { tx.template add<C>(entity); };

using Intensity = std::remove_cvref_t<decltype(w::fields::Light.intensity)>;
using Kind = std::remove_cvref_t<decltype(w::fields::Light.kind)>;
using Column3 = std::remove_cvref_t<decltype(w::fields::WorldMatrix.column3)>;
using LockedFixed = std::remove_cvref_t<decltype(syn::fields::Locked.fixed)>;
using LockedFree = std::remove_cvref_t<decltype(syn::fields::Locked.free)>;

// A set of the wrong kind does not compile; the right kind does.
static_assert(CanSet<Intensity, float>);
static_assert(!CanSet<Intensity, std::array<float, 3>>);
static_assert(CanSet<Column3, std::array<float, 4>>);
static_assert(!CanSet<Column3, std::array<float, 3>>);
static_assert(CanSetInTransaction<Intensity, float>);
static_assert(!CanSetInTransaction<Intensity, std::array<float, 3>>);
// An enum takes its own enumerators, not integers.
static_assert(CanSet<Kind, w::LightKind>);
static_assert(!CanSet<Kind, std::int64_t>);

// A read-only leaf can be read but not set.
static_assert(CanGet<LockedFixed>);
static_assert(!CanSet<LockedFixed, float>);
static_assert(!CanSetInTransaction<LockedFixed, float>);
static_assert(CanSet<LockedFree, float>);

// add(entity, C) only for types whose every leaf is Editable (R10); the bare
// add is always available.
static_assert(CanAddValue<w::Light>);
static_assert(CanAddValue<syn::Probe>);
static_assert(!CanAddValue<syn::Locked>);
static_assert(CanAddBare<syn::Locked>);
static_assert(CanAddBare<w::Light>);

}  // namespace

TEST_CASE("the typed layer's compile-time refusals hold (requires-expressions, with twins)",
          "[gameplay_sdk][typed][compile]") {
  SUCCEED("checked by the static_asserts above");
}
