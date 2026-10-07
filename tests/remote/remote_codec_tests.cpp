#include "codec.h"

#include <atlantis/connection/json.h>
#include <atlantis/world/ecs/world_components.h>
#include <atlantis/world/world_schema.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <variant>
#include <vector>

// Plan 0055 M1 (P2): every value RuntimeConnection carries survives the wire
// -- each PropertyValue alternative (non-finite floats included), each
// command and event kind, each AccessError, tickets, filters, and the World
// schema's descriptors -- after a trip through JSON text.

namespace {

namespace access = atlantis::world::access;
namespace codec = atlantis::remote::codec;
namespace json = atlantis::connection::json;
using atlantis::asset_system::AssetGuid;
using atlantis::asset_system::EntityGuid;
using atlantis::schema::TypeId;

// Encode, write as text, parse back.
[[nodiscard]] json::Value overTheWire(const json::Value& value) {
  auto parsed = json::parse(json::write(value));
  REQUIRE(parsed.isOk());
  return parsed.value();
}

[[nodiscard]] EntityGuid entity(std::uint8_t n) {
  EntityGuid guid;
  guid.bytes[0] = std::byte{0x55};
  guid.bytes[6] = std::byte{0x40};
  guid.bytes[8] = std::byte{0x80};
  guid.bytes[15] = static_cast<std::byte>(n);
  return guid;
}

[[nodiscard]] AssetGuid asset(std::uint8_t n) {
  AssetGuid guid;
  guid.bytes[1] = std::byte{0x55};
  guid.bytes[15] = static_cast<std::byte>(n);
  return guid;
}

template <typename T>
[[nodiscard]] TypeId typeOf() {
  return atlantis::world::ecs::componentTypeId<T>();
}

// Bitwise equality, so NaN payloads and -0.0 count.
[[nodiscard]] bool sameBits(const access::PropertyValue& a, const access::PropertyValue& b) {
  if (a.index() != b.index()) return false;
  if (const auto* fa = std::get_if<float>(&a)) {
    const float fb = std::get<float>(b);
    if (std::isnan(*fa)) return std::isnan(fb);
    return std::memcmp(fa, &fb, sizeof fb) == 0;
  }
  return a == b;
}

}  // namespace

TEST_CASE("codec: every PropertyValue alternative round-trips", "[remote][codec]") {
  const std::vector<access::PropertyValue> values = {
      access::PropertyValue(std::uint64_t{0}),
      access::PropertyValue(std::numeric_limits<std::uint64_t>::max()),
      access::PropertyValue(12.0f),
      access::PropertyValue(-0.0f),
      access::PropertyValue(0.1f),
      access::PropertyValue(std::nanf("")),
      access::PropertyValue(std::numeric_limits<float>::infinity()),
      access::PropertyValue(-std::numeric_limits<float>::infinity()),
      access::PropertyValue(std::array<float, 3>{-39.615f, 3.255f, -5.032f}),
      access::PropertyValue(std::array<float, 4>{1.0f, 0.8f, 0.55f, 1.0f}),
      access::PropertyValue(asset(7)),
      access::PropertyValue(AssetGuid{}),  // nil survives too
      access::PropertyValue(entity(3)),
      access::PropertyValue(EntityGuid{}),
      access::PropertyValue(access::EnumValue{1}),
      access::PropertyValue(access::EnumValue{-5}),
      access::PropertyValue(access::Absent{}),
  };
  for (const access::PropertyValue& value : values) {
    INFO(json::write(codec::encode(value)));
    const auto back = codec::decodePropertyValue(overTheWire(codec::encode(value)));
    REQUIRE(back.has_value());
    CHECK(sameBits(*back, value));
  }
  CHECK(json::write(codec::encode(access::PropertyValue(std::uint64_t{18446744073709551615ULL}))) ==
        R"({"u64":"18446744073709551615"})");
  CHECK(json::write(codec::encode(access::PropertyValue(std::nanf("")))) == R"({"f32":"nan"})");
  CHECK(json::write(codec::encode(access::PropertyValue(access::Absent{}))) == R"({"absent":true})");
}

TEST_CASE("codec: malformed PropertyValues are refused", "[remote][codec]") {
  for (const std::string_view bad :
       {R"({"u64":12})", R"({"u64":"-1"})", R"({"u64":"18446744073709551616"})", R"({"f32":"NaN"})",
        R"({"vec3":[1,2]})", R"({"vec4":[1,2,3,"x"]})", R"({"enum":1.5})", R"({"absent":false})",
        R"({"assetGuid":"nope"})", R"({"f32":1,"u64":"1"})", R"({})", R"([])", R"({"bogus":1})"}) {
    INFO(bad);
    CHECK_FALSE(codec::decodePropertyValue(json::parse(bad).value()).has_value());
  }
}

TEST_CASE("codec: every command kind round-trips", "[remote][codec]") {
  using atlantis::world::Light;
  const access::PropertyAddress address{entity(1), typeOf<Light>(), atlantis::schema::fieldId("world::Light", "intensity")};
  const std::vector<access::Command> commands = {
      access::CreateEntity{entity(1)},
      access::DestroyEntity{entity(2)},
      access::AddComponent{entity(1), typeOf<Light>()},
      access::RemoveComponent{entity(1), typeOf<Light>()},
      access::SetProperty{address, 24.0f},
      access::SetProperty{address, std::nanf("")},
  };
  for (const access::Command& command : commands) {
    INFO(json::write(codec::encode(command)));
    const auto back = codec::decodeCommand(overTheWire(codec::encode(command)));
    REQUIRE(back.has_value());
    REQUIRE(back->index() == command.index());
    if (const auto* set = std::get_if<access::SetProperty>(&command)) {
      const auto& backSet = std::get<access::SetProperty>(*back);
      CHECK(backSet.address == set->address);
      CHECK(sameBits(backSet.value, set->value));
    } else {
      CHECK(json::write(codec::encode(*back)) == json::write(codec::encode(command)));
    }
  }
  CHECK_FALSE(codec::decodeCommand(json::parse(R"({"kind":"Teleport","entity":"x"})").value()).has_value());
}

TEST_CASE("codec: every event kind round-trips", "[remote][codec]") {
  using atlantis::world::Transform;
  const access::PropertyAddress address{entity(4), typeOf<Transform>(),
                                        atlantis::schema::fieldId("world::Transform", "position")};
  const std::vector<access::Event> events = {
      access::EntityCreated{entity(1)},
      access::EntityDestroyed{entity(2)},
      access::ComponentAdded{entity(3), typeOf<Transform>()},
      access::ComponentRemoved{entity(3), typeOf<Transform>()},
      access::PropertyChanged{address, std::array<float, 3>{1.0f, 2.0f, 3.0f}},
  };
  for (const access::Event& event : events) {
    const auto back = codec::decodeEvent(overTheWire(codec::encode(event)));
    REQUIRE(back.has_value());
    CHECK(*back == event);
  }
}

TEST_CASE("codec: failures with every AccessError, tickets and connection errors round-trip", "[remote][codec]") {
  for (int e = 0; e <= static_cast<int>(access::AccessError::ActiveCameraProtected); ++e) {
    const access::CommandFailure failure{access::CommandTicket{static_cast<std::uint64_t>(1000 + e)},
                                         static_cast<access::AccessError>(e)};
    const auto back = codec::decodeFailure(overTheWire(codec::encode(failure)));
    REQUIRE(back.has_value());
    CHECK(*back == failure);
  }
  const access::TransactionTicket ticket{access::CommandTicket{41}, 3};
  CHECK(codec::decodeTransactionTicket(overTheWire(codec::encode(ticket))) == ticket);
  CHECK(codec::decodeTransactionTicket(overTheWire(codec::encode(access::TransactionTicket{}))) ==
        access::TransactionTicket{});
  CHECK(codec::decodeConnectionError(codec::encodeConnectionError(
            atlantis::connection::ConnectionError::UnknownSubscription)) ==
        atlantis::connection::ConnectionError::UnknownSubscription);
  CHECK_FALSE(codec::decodeAccessError(json::Value::string("Bogus")).has_value());
}

TEST_CASE("codec: event filters round-trip", "[remote][codec]") {
  using atlantis::connection::EventFilter;
  using atlantis::connection::EventKind;
  using atlantis::connection::EventKindSet;
  const std::vector<EventFilter> filters = {
      EventFilter{},
      EventFilter{EventKindSet(), {}, {}},
      EventFilter{EventKindSet::only(EventKind::PropertyChanged), entity(9), typeOf<atlantis::world::Light>()},
      EventFilter{EventKindSet::only(EventKind::EntityCreated).with(EventKind::EntityDestroyed), {}, {}},
  };
  for (const EventFilter& filter : filters) {
    const auto back = codec::decodeFilter(overTheWire(codec::encode(filter)));
    REQUIRE(back.has_value());
    CHECK(back->kinds == filter.kinds);
    CHECK(back->entity == filter.entity);
    CHECK(back->component == filter.component);
  }
}

TEST_CASE("codec: TypeIds are 0x plus 16 hex digits", "[remote][codec]") {
  CHECK(json::write(codec::encodeTypeId(TypeId{0x00ab})) == R"("0x00000000000000ab")");
  CHECK(codec::decodeTypeId(json::Value::string("0xffffffffffffffff")) == TypeId{~0ULL});
  CHECK_FALSE(codec::decodeTypeId(json::Value::string("0xFFFFFFFFFFFFFFFF")).has_value());  // lowercase only
  CHECK_FALSE(codec::decodeTypeId(json::Value::string("0x1")).has_value());
}

TEST_CASE("codec: the World schema's descriptors survive, field by field", "[remote][codec]") {
  const auto schema = atlantis::world::worldSchema();
  codec::OwnedSchema decoded;
  REQUIRE(decoded.decode(overTheWire(codec::encodeSchema(schema))));
  REQUIRE(decoded.types().size() == schema.size());
  for (std::size_t t = 0; t < schema.size(); ++t) {
    const auto& a = schema[t];
    const auto& b = decoded.types()[t];
    INFO(a.name);
    CHECK(a.id == b.id);
    CHECK(a.name == b.name);
    CHECK(a.kind == b.kind);
    CHECK(a.schemaVersion == b.schemaVersion);
    REQUIRE(a.fields.size() == b.fields.size());
    for (std::size_t f = 0; f < a.fields.size(); ++f) {
      CHECK(a.fields[f].id == b.fields[f].id);
      CHECK(a.fields[f].name == b.fields[f].name);
      CHECK(a.fields[f].kind == b.fields[f].kind);
      CHECK(a.fields[f].primitive == b.fields[f].primitive);
      CHECK(a.fields[f].type == b.fields[f].type);
      CHECK(a.fields[f].flags == b.fields[f].flags);
      CHECK(a.fields[f].byteOffset == b.fields[f].byteOffset);
    }
    REQUIRE(a.constants.size() == b.constants.size());
    for (std::size_t c = 0; c < a.constants.size(); ++c) {
      CHECK(a.constants[c].name == b.constants[c].name);
      CHECK(a.constants[c].value == b.constants[c].value);
    }
  }
  // The decoded descriptors view the owner's own storage, not the source's.
  CHECK(decoded.types()[0].name.data() != schema[0].name.data());
  CHECK_FALSE(decoded.decode(json::parse(R"([{"id":"0x1"}])").value()));
}
