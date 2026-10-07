#pragma once

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/connection/json.h>
#include <atlantis/connection/runtime_connection.h>
#include <atlantis/schema.h>
#include <atlantis/world/access/access_error.h>
#include <atlantis/world/access/runtime_world_access.h>

#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Plan 0055 P2 (ADR-0106 D1, D2): the wire encoding of RuntimeConnection's
// value types, private to Atlantis Remote. Tagged so every variant
// alternative survives the trip:
//   PropertyValue  {"u64":"<decimal>"} {"f32":x} {"vec3":[x,y,z]} {"vec4":[..]}
//                  {"assetGuid":"<guid>"} {"entityGuid":"<guid>"} {"enum":n} {"absent":true}
//   float          a JSON number, or "nan" / "inf" / "-inf" (so the boundary's
//                  NonFiniteValue refusal stays reachable over the wire)
//   TypeId/FieldId "0x" + 16 lowercase hex digits
//   Command/Event  {"kind":"<Name>", ...fields}
//   Result         {"ok":<value>} or {"err":"<error name>"}
// Decoders return nullopt for anything malformed; the caller turns that into
// a protocol error. Pure functions.
namespace atlantis::remote::codec {

using connection::json::Value;
namespace access = atlantis::world::access;

[[nodiscard]] Value encodeFloat(float value);
[[nodiscard]] std::optional<float> decodeFloat(const Value& value);

[[nodiscard]] Value encodeId(std::uint64_t id);  // TypeId / FieldId
[[nodiscard]] std::optional<std::uint64_t> decodeId(const Value& value);
[[nodiscard]] Value encodeTypeId(schema::TypeId id);
[[nodiscard]] std::optional<schema::TypeId> decodeTypeId(const Value& value);

[[nodiscard]] Value encodeEntity(const atlantis::asset_system::EntityGuid& entity);
[[nodiscard]] std::optional<atlantis::asset_system::EntityGuid> decodeEntity(const Value& value);
[[nodiscard]] Value encodeAsset(const atlantis::asset_system::AssetGuid& asset);
[[nodiscard]] std::optional<atlantis::asset_system::AssetGuid> decodeAsset(const Value& value);

[[nodiscard]] std::optional<std::uint64_t> decodeUInt64(const Value& value);  // a JSON integer

[[nodiscard]] Value encodeAccessError(access::AccessError error);
[[nodiscard]] std::optional<access::AccessError> decodeAccessError(const Value& value);
[[nodiscard]] Value encodeConnectionError(connection::ConnectionError error);
[[nodiscard]] std::optional<connection::ConnectionError> decodeConnectionError(const Value& value);

[[nodiscard]] Value encode(const access::PropertyValue& value);
[[nodiscard]] std::optional<access::PropertyValue> decodePropertyValue(const Value& value);
[[nodiscard]] Value encode(const access::PropertyAddress& address);
[[nodiscard]] std::optional<access::PropertyAddress> decodeAddress(const Value& value);
[[nodiscard]] Value encode(const access::Command& command);
[[nodiscard]] std::optional<access::Command> decodeCommand(const Value& value);
[[nodiscard]] Value encode(const access::Event& event);
[[nodiscard]] std::optional<access::Event> decodeEvent(const Value& value);
[[nodiscard]] Value encode(const access::CommandFailure& failure);
[[nodiscard]] std::optional<access::CommandFailure> decodeFailure(const Value& value);
[[nodiscard]] Value encode(const access::TransactionTicket& ticket);
[[nodiscard]] std::optional<access::TransactionTicket> decodeTransactionTicket(const Value& value);
[[nodiscard]] Value encode(const connection::EventFilter& filter);
[[nodiscard]] std::optional<connection::EventFilter> decodeFilter(const Value& value);

// {"ok":<value>} / {"err":"<name>"}
[[nodiscard]] Value ok(Value value);
[[nodiscard]] Value err(Value name);

// A schema decoded from the wire, owning every string and table its
// descriptors view (Spec 0054 ruling Q8, K1: valid while the owner lives).
// Not copyable or movable: the descriptors point into it.
class OwnedSchema {
 public:
  OwnedSchema() = default;
  OwnedSchema(const OwnedSchema&) = delete;
  OwnedSchema& operator=(const OwnedSchema&) = delete;

  [[nodiscard]] std::span<const schema::TypeDescriptor> types() const noexcept { return types_; }
  // Replaces the contents with the decoded `value`; false if malformed.
  [[nodiscard]] bool decode(const Value& value);

 private:
  std::deque<std::string> strings_;  // stable addresses for the string_views
  std::deque<std::vector<schema::FieldDescriptor>> fields_;
  std::deque<std::vector<schema::EnumConstantDescriptor>> constants_;
  std::vector<schema::TypeDescriptor> types_;
};

[[nodiscard]] Value encodeSchema(std::span<const schema::TypeDescriptor> types);

}  // namespace atlantis::remote::codec
