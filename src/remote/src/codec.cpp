#include "codec.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <type_traits>
#include <utility>
#include <variant>

namespace atlantis::remote::codec {

using atlantis::asset_system::AssetGuid;
using atlantis::asset_system::EntityGuid;
using connection::EventFilter;
using connection::EventKind;
using connection::EventKindSet;

namespace {

constexpr std::string_view kNilGuid = "00000000-0000-0000-0000-000000000000";

constexpr std::array kAccessErrors = {
    access::AccessError::UnknownEntity,           access::AccessError::NilGuid,
    access::AccessError::DuplicateGuid,           access::AccessError::UnknownComponentType,
    access::AccessError::ComponentMissing,        access::AccessError::ComponentAlreadyPresent,
    access::AccessError::UnknownField,            access::AccessError::FieldNotEditable,
    access::AccessError::KindMismatch,            access::AccessError::EnumValueOutOfRange,
    access::AccessError::NonFiniteValue,          access::AccessError::LightLimitExceeded,
    access::AccessError::ActiveCameraProtected,
};

constexpr std::array<std::pair<EventKind, std::string_view>, 5> kEventKinds = {{
    {EventKind::EntityCreated, "EntityCreated"},
    {EventKind::EntityDestroyed, "EntityDestroyed"},
    {EventKind::ComponentAdded, "ComponentAdded"},
    {EventKind::ComponentRemoved, "ComponentRemoved"},
    {EventKind::PropertyChanged, "PropertyChanged"},
}};

// Decimal digits only (no sign, no space), in range.
[[nodiscard]] std::optional<std::uint64_t> decimalUInt64(const std::string& text) {
  if (text.empty() || text.size() > 20) return std::nullopt;
  std::uint64_t out = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') return std::nullopt;
    const std::uint64_t digit = static_cast<std::uint64_t>(c - '0');
    if (out > (UINT64_MAX - digit) / 10) return std::nullopt;
    out = out * 10 + digit;
  }
  return out;
}

[[nodiscard]] const Value* member(const Value& object, std::string_view key) { return object.find(key); }

[[nodiscard]] std::optional<std::string_view> kindOf(const Value& value) {
  const Value* kind = member(value, "kind");
  if (kind == nullptr || !kind->isString()) return std::nullopt;
  return std::string_view(kind->asString());
}

template <std::size_t N>
[[nodiscard]] Value encodeVector(const std::array<float, N>& v) {
  Value out = Value::array();
  for (const float x : v) out.push(encodeFloat(x));
  return out;
}

template <std::size_t N>
[[nodiscard]] std::optional<std::array<float, N>> decodeVector(const Value& value) {
  if (!value.isArray() || value.asArray().size() != N) return std::nullopt;
  std::array<float, N> out{};
  for (std::size_t i = 0; i < N; ++i) {
    const auto x = decodeFloat(value.asArray()[i]);
    if (!x) return std::nullopt;
    out[i] = *x;
  }
  return out;
}

[[nodiscard]] std::string_view kindName(schema::TypeKind kind) {
  switch (kind) {
    case schema::TypeKind::Primitive: return "Primitive";
    case schema::TypeKind::Struct: return "Struct";
    case schema::TypeKind::Enum: return "Enum";
  }
  return "?";
}

[[nodiscard]] std::optional<schema::TypeKind> decodeKind(const Value& value) {
  if (!value.isString()) return std::nullopt;
  for (const auto kind : {schema::TypeKind::Primitive, schema::TypeKind::Struct, schema::TypeKind::Enum}) {
    if (value.asString() == kindName(kind)) return kind;
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<schema::PrimitiveKind> decodePrimitive(const Value& value) {
  if (!value.isString()) return std::nullopt;
  for (const auto kind : {schema::PrimitiveKind::UInt64, schema::PrimitiveKind::Float32,
                          schema::PrimitiveKind::Vec3Float32, schema::PrimitiveKind::Vec4Float32,
                          schema::PrimitiveKind::AssetGuid, schema::PrimitiveKind::EntityGuid}) {
    if (value.asString() == schema::toString(kind)) return kind;
  }
  return std::nullopt;
}

}  // namespace

Value encodeFloat(float value) {
  if (std::isnan(value)) return Value::string("nan");
  if (std::isinf(value)) return Value::string(value > 0 ? "inf" : "-inf");
  return Value::number(value);
}

std::optional<float> decodeFloat(const Value& value) {
  if (value.isString()) {
    if (value.asString() == "nan") return std::nanf("");
    if (value.asString() == "inf") return HUGE_VALF;
    if (value.asString() == "-inf") return -HUGE_VALF;
    return std::nullopt;
  }
  float out = 0.0f;
  if (!value.toFloat(out)) return std::nullopt;
  return out;
}

Value encodeId(std::uint64_t id) {
  std::array<char, 24> buffer{};
  std::snprintf(buffer.data(), buffer.size(), "0x%016llx", static_cast<unsigned long long>(id));
  return Value::string(buffer.data());
}

std::optional<std::uint64_t> decodeId(const Value& value) {
  if (!value.isString()) return std::nullopt;
  const std::string& text = value.asString();
  if (text.size() != 18 || text[0] != '0' || text[1] != 'x') return std::nullopt;
  std::uint64_t out = 0;
  for (std::size_t i = 2; i < text.size(); ++i) {
    const char c = text[i];
    out <<= 4;
    if (c >= '0' && c <= '9') {
      out |= static_cast<std::uint64_t>(c - '0');
    } else if (c >= 'a' && c <= 'f') {
      out |= static_cast<std::uint64_t>(c - 'a' + 10);
    } else {
      return std::nullopt;
    }
  }
  return out;
}

Value encodeTypeId(schema::TypeId id) { return encodeId(id.value); }

std::optional<schema::TypeId> decodeTypeId(const Value& value) {
  const auto id = decodeId(value);
  if (!id) return std::nullopt;
  return schema::TypeId{*id};
}

Value encodeEntity(const EntityGuid& entity) {
  return Value::string(entity == EntityGuid{} ? std::string(kNilGuid) : atlantis::asset_system::toString(entity));
}

std::optional<EntityGuid> decodeEntity(const Value& value) {
  if (!value.isString()) return std::nullopt;
  if (value.asString() == kNilGuid) return EntityGuid{};
  auto parsed = atlantis::asset_system::parseEntityGuid(value.asString());
  if (parsed.isErr()) return std::nullopt;
  return parsed.value();
}

Value encodeAsset(const AssetGuid& asset) {
  return Value::string(asset == AssetGuid{} ? std::string(kNilGuid) : atlantis::asset_system::toString(asset));
}

std::optional<AssetGuid> decodeAsset(const Value& value) {
  if (!value.isString()) return std::nullopt;
  if (value.asString() == kNilGuid) return AssetGuid{};
  auto parsed = atlantis::asset_system::parseAssetGuid(value.asString());
  if (parsed.isErr()) return std::nullopt;
  return parsed.value();
}

std::optional<std::uint64_t> decodeUInt64(const Value& value) {
  std::uint64_t out = 0;
  if (!value.toUInt64(out)) return std::nullopt;
  return out;
}

Value encodeAccessError(access::AccessError error) { return Value::string(std::string(access::toString(error))); }

std::optional<access::AccessError> decodeAccessError(const Value& value) {
  if (!value.isString()) return std::nullopt;
  for (const access::AccessError error : kAccessErrors) {
    if (value.asString() == access::toString(error)) return error;
  }
  return std::nullopt;
}

Value encodeConnectionError(connection::ConnectionError error) {
  return Value::string(std::string(connection::toString(error)));
}

std::optional<connection::ConnectionError> decodeConnectionError(const Value& value) {
  if (value.isString() && value.asString() == connection::toString(connection::ConnectionError::UnknownSubscription)) {
    return connection::ConnectionError::UnknownSubscription;
  }
  return std::nullopt;
}

Value encode(const access::PropertyValue& value) {
  return std::visit(
      [](const auto& v) {
        using V = std::decay_t<decltype(v)>;
        Value out = Value::object();
        if constexpr (std::is_same_v<V, std::uint64_t>) {
          out.set("u64", Value::string(std::to_string(v)));
        } else if constexpr (std::is_same_v<V, float>) {
          out.set("f32", encodeFloat(v));
        } else if constexpr (std::is_same_v<V, std::array<float, 3>>) {
          out.set("vec3", encodeVector(v));
        } else if constexpr (std::is_same_v<V, std::array<float, 4>>) {
          out.set("vec4", encodeVector(v));
        } else if constexpr (std::is_same_v<V, AssetGuid>) {
          out.set("assetGuid", encodeAsset(v));
        } else if constexpr (std::is_same_v<V, EntityGuid>) {
          out.set("entityGuid", encodeEntity(v));
        } else if constexpr (std::is_same_v<V, access::EnumValue>) {
          out.set("enum", Value::number(v.value));
        } else {
          static_assert(std::is_same_v<V, access::Absent>);
          out.set("absent", Value::boolean(true));
        }
        return out;
      },
      value);
}

std::optional<access::PropertyValue> decodePropertyValue(const Value& value) {
  if (!value.isObject() || value.asObject().size() != 1) return std::nullopt;
  const auto& [tag, payload] = value.asObject().front();
  if (tag == "u64") {
    const auto number = payload.isString() ? decimalUInt64(payload.asString()) : std::nullopt;
    if (!number) return std::nullopt;
    return access::PropertyValue(*number);
  }
  if (tag == "f32") {
    const auto f = decodeFloat(payload);
    if (!f) return std::nullopt;
    return access::PropertyValue(*f);
  }
  if (tag == "vec3") {
    const auto v = decodeVector<3>(payload);
    if (!v) return std::nullopt;
    return access::PropertyValue(*v);
  }
  if (tag == "vec4") {
    const auto v = decodeVector<4>(payload);
    if (!v) return std::nullopt;
    return access::PropertyValue(*v);
  }
  if (tag == "assetGuid") {
    const auto g = decodeAsset(payload);
    if (!g) return std::nullopt;
    return access::PropertyValue(*g);
  }
  if (tag == "entityGuid") {
    const auto g = decodeEntity(payload);
    if (!g) return std::nullopt;
    return access::PropertyValue(*g);
  }
  if (tag == "enum") {
    std::int64_t out = 0;
    if (!payload.toInt64(out)) return std::nullopt;
    return access::PropertyValue(access::EnumValue{out});
  }
  if (tag == "absent" && payload.isBool() && payload.asBool()) return access::PropertyValue(access::Absent{});
  return std::nullopt;
}

Value encode(const access::PropertyAddress& address) {
  Value out = Value::object();
  out.set("entity", encodeEntity(address.entity));
  out.set("component", encodeTypeId(address.component));
  out.set("field", encodeId(address.field.value));
  return out;
}

std::optional<access::PropertyAddress> decodeAddress(const Value& value) {
  const Value* entity = member(value, "entity");
  const Value* component = member(value, "component");
  const Value* field = member(value, "field");
  if (entity == nullptr || component == nullptr || field == nullptr) return std::nullopt;
  const auto e = decodeEntity(*entity);
  const auto c = decodeTypeId(*component);
  const auto f = decodeId(*field);
  if (!e || !c || !f) return std::nullopt;
  return access::PropertyAddress{*e, *c, schema::FieldId{*f}};
}

Value encode(const access::Command& command) {
  return std::visit(
      [](const auto& c) {
        using C = std::decay_t<decltype(c)>;
        Value out = Value::object();
        if constexpr (std::is_same_v<C, access::CreateEntity>) {
          out.set("kind", Value::string("CreateEntity"));
          out.set("entity", encodeEntity(c.entity));
        } else if constexpr (std::is_same_v<C, access::DestroyEntity>) {
          out.set("kind", Value::string("DestroyEntity"));
          out.set("entity", encodeEntity(c.entity));
        } else if constexpr (std::is_same_v<C, access::AddComponent>) {
          out.set("kind", Value::string("AddComponent"));
          out.set("entity", encodeEntity(c.entity));
          out.set("component", encodeTypeId(c.component));
        } else if constexpr (std::is_same_v<C, access::RemoveComponent>) {
          out.set("kind", Value::string("RemoveComponent"));
          out.set("entity", encodeEntity(c.entity));
          out.set("component", encodeTypeId(c.component));
        } else {
          static_assert(std::is_same_v<C, access::SetProperty>);
          out.set("kind", Value::string("SetProperty"));
          out.set("address", encode(c.address));
          out.set("value", encode(c.value));
        }
        return out;
      },
      command);
}

namespace {

// The entity (and component) of the four entity/component-shaped commands and
// events.
struct EntityComponent {
  EntityGuid entity;
  schema::TypeId component;
};
[[nodiscard]] std::optional<EntityGuid> entityOf(const Value& value) {
  const Value* entity = member(value, "entity");
  if (entity == nullptr) return std::nullopt;
  return decodeEntity(*entity);
}
[[nodiscard]] std::optional<EntityComponent> entityComponentOf(const Value& value) {
  const auto entity = entityOf(value);
  const Value* component = member(value, "component");
  if (!entity || component == nullptr) return std::nullopt;
  const auto c = decodeTypeId(*component);
  if (!c) return std::nullopt;
  return EntityComponent{*entity, *c};
}
[[nodiscard]] std::optional<std::pair<access::PropertyAddress, access::PropertyValue>> addressValueOf(
    const Value& value) {
  const Value* address = member(value, "address");
  const Value* v = member(value, "value");
  if (address == nullptr || v == nullptr) return std::nullopt;
  const auto a = decodeAddress(*address);
  const auto pv = decodePropertyValue(*v);
  if (!a || !pv) return std::nullopt;
  return std::make_pair(*a, *pv);
}

}  // namespace

std::optional<access::Command> decodeCommand(const Value& value) {
  const auto kind = kindOf(value);
  if (!kind) return std::nullopt;
  if (*kind == "CreateEntity" || *kind == "DestroyEntity") {
    const auto entity = entityOf(value);
    if (!entity) return std::nullopt;
    if (*kind == "CreateEntity") return access::Command(access::CreateEntity{*entity});
    return access::Command(access::DestroyEntity{*entity});
  }
  if (*kind == "AddComponent" || *kind == "RemoveComponent") {
    const auto ec = entityComponentOf(value);
    if (!ec) return std::nullopt;
    if (*kind == "AddComponent") return access::Command(access::AddComponent{ec->entity, ec->component});
    return access::Command(access::RemoveComponent{ec->entity, ec->component});
  }
  if (*kind == "SetProperty") {
    const auto av = addressValueOf(value);
    if (!av) return std::nullopt;
    return access::Command(access::SetProperty{av->first, av->second});
  }
  return std::nullopt;
}

Value encode(const access::Event& event) {
  return std::visit(
      [](const auto& e) {
        using E = std::decay_t<decltype(e)>;
        Value out = Value::object();
        if constexpr (std::is_same_v<E, access::EntityCreated>) {
          out.set("kind", Value::string("EntityCreated"));
          out.set("entity", encodeEntity(e.entity));
        } else if constexpr (std::is_same_v<E, access::EntityDestroyed>) {
          out.set("kind", Value::string("EntityDestroyed"));
          out.set("entity", encodeEntity(e.entity));
        } else if constexpr (std::is_same_v<E, access::ComponentAdded>) {
          out.set("kind", Value::string("ComponentAdded"));
          out.set("entity", encodeEntity(e.entity));
          out.set("component", encodeTypeId(e.component));
        } else if constexpr (std::is_same_v<E, access::ComponentRemoved>) {
          out.set("kind", Value::string("ComponentRemoved"));
          out.set("entity", encodeEntity(e.entity));
          out.set("component", encodeTypeId(e.component));
        } else {
          static_assert(std::is_same_v<E, access::PropertyChanged>);
          out.set("kind", Value::string("PropertyChanged"));
          out.set("address", encode(e.address));
          out.set("value", encode(e.value));
        }
        return out;
      },
      event);
}

std::optional<access::Event> decodeEvent(const Value& value) {
  const auto kind = kindOf(value);
  if (!kind) return std::nullopt;
  if (*kind == "EntityCreated" || *kind == "EntityDestroyed") {
    const auto entity = entityOf(value);
    if (!entity) return std::nullopt;
    if (*kind == "EntityCreated") return access::Event(access::EntityCreated{*entity});
    return access::Event(access::EntityDestroyed{*entity});
  }
  if (*kind == "ComponentAdded" || *kind == "ComponentRemoved") {
    const auto ec = entityComponentOf(value);
    if (!ec) return std::nullopt;
    if (*kind == "ComponentAdded") return access::Event(access::ComponentAdded{ec->entity, ec->component});
    return access::Event(access::ComponentRemoved{ec->entity, ec->component});
  }
  if (*kind == "PropertyChanged") {
    const auto av = addressValueOf(value);
    if (!av) return std::nullopt;
    return access::Event(access::PropertyChanged{av->first, av->second});
  }
  return std::nullopt;
}

Value encode(const access::CommandFailure& failure) {
  Value out = Value::object();
  out.set("ticket", Value::number(failure.ticket.value));
  out.set("error", encodeAccessError(failure.error));
  return out;
}

std::optional<access::CommandFailure> decodeFailure(const Value& value) {
  const Value* ticket = member(value, "ticket");
  const Value* error = member(value, "error");
  if (ticket == nullptr || error == nullptr) return std::nullopt;
  const auto t = decodeUInt64(*ticket);
  const auto e = decodeAccessError(*error);
  if (!t || !e) return std::nullopt;
  return access::CommandFailure{access::CommandTicket{*t}, *e};
}

Value encode(const access::TransactionTicket& ticket) {
  Value out = Value::object();
  out.set("first", Value::number(ticket.first.value));
  out.set("count", Value::number(ticket.count));
  return out;
}

std::optional<access::TransactionTicket> decodeTransactionTicket(const Value& value) {
  const Value* first = member(value, "first");
  const Value* count = member(value, "count");
  if (first == nullptr || count == nullptr) return std::nullopt;
  const auto f = decodeUInt64(*first);
  const auto c = decodeUInt64(*count);
  if (!f || !c) return std::nullopt;
  return access::TransactionTicket{access::CommandTicket{*f}, *c};
}

Value encode(const EventFilter& filter) {
  Value kinds = Value::array();
  for (const auto& [kind, name] : kEventKinds) {
    if (filter.kinds.contains(kind)) kinds.push(Value::string(std::string(name)));
  }
  Value out = Value::object();
  out.set("kinds", std::move(kinds));
  out.set("entity", filter.entity ? encodeEntity(*filter.entity) : Value());
  out.set("component", filter.component ? encodeTypeId(*filter.component) : Value());
  return out;
}

std::optional<EventFilter> decodeFilter(const Value& value) {
  const Value* kinds = member(value, "kinds");
  const Value* entity = member(value, "entity");
  const Value* component = member(value, "component");
  if (kinds == nullptr || !kinds->isArray() || entity == nullptr || component == nullptr) return std::nullopt;
  EventFilter filter;
  filter.kinds = EventKindSet();
  for (const Value& name : kinds->asArray()) {
    if (!name.isString()) return std::nullopt;
    bool known = false;
    for (const auto& [kind, kindText] : kEventKinds) {
      if (name.asString() == kindText) {
        filter.kinds = filter.kinds.with(kind);
        known = true;
      }
    }
    if (!known) return std::nullopt;
  }
  if (!entity->isNull()) {
    const auto e = decodeEntity(*entity);
    if (!e) return std::nullopt;
    filter.entity = *e;
  }
  if (!component->isNull()) {
    const auto c = decodeTypeId(*component);
    if (!c) return std::nullopt;
    filter.component = *c;
  }
  return filter;
}

Value ok(Value value) {
  Value out = Value::object();
  out.set("ok", std::move(value));
  return out;
}

Value err(Value name) {
  Value out = Value::object();
  out.set("err", std::move(name));
  return out;
}

Value encodeSchema(std::span<const schema::TypeDescriptor> types) {
  Value out = Value::array();
  for (const schema::TypeDescriptor& type : types) {
    Value fields = Value::array();
    for (const schema::FieldDescriptor& field : type.fields) {
      Value f = Value::object();
      f.set("id", encodeId(field.id.value));
      f.set("name", Value::string(std::string(field.name)));
      f.set("kind", Value::string(std::string(kindName(field.kind))));
      f.set("primitive", Value::string(std::string(schema::toString(field.primitive))));
      f.set("type", encodeTypeId(field.type));
      f.set("flags", Value::number(static_cast<std::uint64_t>(field.flags)));
      f.set("offset", Value::number(static_cast<std::uint64_t>(field.byteOffset)));
      fields.push(std::move(f));
    }
    Value constants = Value::array();
    for (const schema::EnumConstantDescriptor& constant : type.constants) {
      Value c = Value::object();
      c.set("name", Value::string(std::string(constant.name)));
      c.set("value", Value::number(constant.value));
      constants.push(std::move(c));
    }
    Value t = Value::object();
    t.set("id", encodeTypeId(type.id));
    t.set("name", Value::string(std::string(type.name)));
    t.set("kind", Value::string(std::string(kindName(type.kind))));
    t.set("version", Value::number(static_cast<std::uint64_t>(type.schemaVersion)));
    t.set("fields", std::move(fields));
    t.set("constants", std::move(constants));
    out.push(std::move(t));
  }
  return out;
}

bool OwnedSchema::decode(const Value& value) {
  strings_.clear();
  fields_.clear();
  constants_.clear();
  types_.clear();
  if (!value.isArray()) return false;
  const auto keep = [&](const std::string& text) -> std::string_view { return strings_.emplace_back(text); };
  for (const Value& t : value.asArray()) {
    const Value* id = member(t, "id");
    const Value* name = member(t, "name");
    const Value* kind = member(t, "kind");
    const Value* version = member(t, "version");
    const Value* fields = member(t, "fields");
    const Value* constants = member(t, "constants");
    if (!id || !name || !name->isString() || !kind || !version || !fields || !fields->isArray() || !constants ||
        !constants->isArray()) {
      return false;
    }
    schema::TypeDescriptor type;
    const auto typeId = decodeTypeId(*id);
    const auto typeKind = decodeKind(*kind);
    const auto typeVersion = decodeUInt64(*version);
    if (!typeId || !typeKind || !typeVersion) return false;
    type.id = *typeId;
    type.name = keep(name->asString());
    type.kind = *typeKind;
    type.schemaVersion = static_cast<std::uint32_t>(*typeVersion);
    std::vector<schema::FieldDescriptor>& fieldTable = fields_.emplace_back();
    for (const Value& f : fields->asArray()) {
      const Value* fid = member(f, "id");
      const Value* fname = member(f, "name");
      const Value* fkind = member(f, "kind");
      const Value* fprimitive = member(f, "primitive");
      const Value* ftype = member(f, "type");
      const Value* fflags = member(f, "flags");
      const Value* foffset = member(f, "offset");
      if (!fid || !fname || !fname->isString() || !fkind || !fprimitive || !ftype || !fflags || !foffset) return false;
      const auto fieldId = decodeId(*fid);
      const auto fieldKind = decodeKind(*fkind);
      const auto primitive = decodePrimitive(*fprimitive);
      const auto fieldType = decodeTypeId(*ftype);
      const auto flags = decodeUInt64(*fflags);
      const auto offset = decodeUInt64(*foffset);
      if (!fieldId || !fieldKind || !primitive || !fieldType || !flags || !offset) return false;
      schema::FieldDescriptor field;
      field.id = schema::FieldId{*fieldId};
      field.name = keep(fname->asString());
      field.kind = *fieldKind;
      field.primitive = *primitive;
      field.type = *fieldType;
      field.flags = static_cast<schema::FieldFlags>(*flags);
      field.byteOffset = static_cast<std::uint32_t>(*offset);
      fieldTable.push_back(field);
    }
    std::vector<schema::EnumConstantDescriptor>& constantTable = constants_.emplace_back();
    for (const Value& c : constants->asArray()) {
      const Value* cname = member(c, "name");
      const Value* cvalue = member(c, "value");
      std::int64_t number = 0;
      if (!cname || !cname->isString() || !cvalue || !cvalue->toInt64(number)) return false;
      constantTable.push_back(schema::EnumConstantDescriptor{keep(cname->asString()), number});
    }
    type.fields = fieldTable;
    type.constants = constantTable;
    types_.push_back(type);
  }
  return true;
}

// Plan 0055 M3: RuntimeControl's value types (P5, P6).

namespace {

constexpr std::array kControlErrors = {
    connection::ControlError::InvalidRequest,
    connection::ControlError::NotRendering,
    connection::ControlError::CaptureFailed,
    connection::ControlError::Stopped,
};

constexpr std::array kSeverities = {
    connection::DiagnosticSeverity::Warning,
    connection::DiagnosticSeverity::Error,
    connection::DiagnosticSeverity::Fatal,
};

template <std::size_t N>
[[nodiscard]] Value encodeFloats(const std::array<float, N>& values) {
  return encodeVector(values);
}

[[nodiscard]] std::optional<float> floatMember(const Value& object, std::string_view key) {
  const Value* value = member(object, key);
  if (value == nullptr) return std::nullopt;
  return decodeFloat(*value);
}

template <std::size_t N>
[[nodiscard]] std::optional<std::array<float, N>> floatsMember(const Value& object, std::string_view key) {
  const Value* value = member(object, key);
  if (value == nullptr) return std::nullopt;
  return decodeVector<N>(*value);
}

}  // namespace

Value encodeControlError(connection::ControlError error) {
  return Value::string(std::string(connection::toString(error)));
}

std::optional<connection::ControlError> decodeControlError(const Value& value) {
  if (!value.isString()) return std::nullopt;
  for (const connection::ControlError error : kControlErrors) {
    if (value.asString() == connection::toString(error)) return error;
  }
  return std::nullopt;
}

Value encode(const connection::RuntimeStatus& status) {
  Value out = Value::object();
  out.set("paused", Value::boolean(status.paused));
  out.set("frame", Value::number(status.frame));
  out.set("scene", encodeAsset(status.scene));
  return out;
}

std::optional<connection::RuntimeStatus> decodeStatus(const Value& value) {
  const Value* paused = member(value, "paused");
  const Value* frame = member(value, "frame");
  const Value* scene = member(value, "scene");
  if (paused == nullptr || !paused->isBool() || frame == nullptr || scene == nullptr) return std::nullopt;
  const auto f = decodeUInt64(*frame);
  const auto s = decodeAsset(*scene);
  if (!f || !s) return std::nullopt;
  return connection::RuntimeStatus{paused->asBool(), *f, *s};
}

Value encode(const connection::StepRequest& request) {
  Value out = Value::object();
  out.set("frames", Value::number(static_cast<std::uint64_t>(request.frames)));
  out.set("image", request.imagePath ? Value::string(*request.imagePath) : Value());
  return out;
}

std::optional<connection::StepRequest> decodeStepRequest(const Value& value) {
  const Value* frames = member(value, "frames");
  const Value* image = member(value, "image");
  if (frames == nullptr || image == nullptr || !(image->isNull() || image->isString())) return std::nullopt;
  const auto n = decodeUInt64(*frames);
  if (!n || *n > UINT32_MAX) return std::nullopt;
  connection::StepRequest request;
  request.frames = static_cast<std::uint32_t>(*n);
  if (image->isString()) request.imagePath = image->asString();
  return request;
}

Value encode(const connection::FrameData& data) {
  Value directional = Value::array();
  for (const connection::FrameDirectionalLight& light : data.directionalLights) {
    Value l = Value::object();
    l.set("direction", encodeFloats(light.direction));
    l.set("color", encodeFloats(light.color));
    l.set("intensity", encodeFloat(light.intensity));
    directional.push(std::move(l));
  }
  Value points = Value::array();
  for (const connection::FramePointLight& light : data.pointLights) {
    Value l = Value::object();
    l.set("position", encodeFloats(light.position));
    l.set("color", encodeFloats(light.color));
    l.set("intensity", encodeFloat(light.intensity));
    l.set("range", encodeFloat(light.range));
    points.push(std::move(l));
  }
  Value out = Value::object();
  out.set("directionalLights", std::move(directional));
  out.set("pointLights", std::move(points));
  out.set("view", encodeFloats(data.view));
  out.set("projection", encodeFloats(data.projection));
  out.set("drawItemCount", Value::number(data.drawItemCount));
  return out;
}

std::optional<connection::FrameData> decodeFrameData(const Value& value) {
  const Value* directional = member(value, "directionalLights");
  const Value* points = member(value, "pointLights");
  const Value* drawItems = member(value, "drawItemCount");
  if (directional == nullptr || !directional->isArray() || points == nullptr || !points->isArray() ||
      drawItems == nullptr) {
    return std::nullopt;
  }
  connection::FrameData data;
  for (const Value& l : directional->asArray()) {
    const auto direction = floatsMember<3>(l, "direction");
    const auto color = floatsMember<3>(l, "color");
    const auto intensity = floatMember(l, "intensity");
    if (!direction || !color || !intensity) return std::nullopt;
    data.directionalLights.push_back({*direction, *color, *intensity});
  }
  for (const Value& l : points->asArray()) {
    const auto position = floatsMember<3>(l, "position");
    const auto color = floatsMember<3>(l, "color");
    const auto intensity = floatMember(l, "intensity");
    const auto range = floatMember(l, "range");
    if (!position || !color || !intensity || !range) return std::nullopt;
    data.pointLights.push_back({*position, *color, *intensity, *range});
  }
  const auto view = floatsMember<16>(value, "view");
  const auto projection = floatsMember<16>(value, "projection");
  const auto count = decodeUInt64(*drawItems);
  if (!view || !projection || !count) return std::nullopt;
  data.view = *view;
  data.projection = *projection;
  data.drawItemCount = *count;
  return data;
}

Value encode(const connection::FrameReport& report) {
  Value out = Value::object();
  out.set("frame", Value::number(report.frame));
  out.set("applied", Value::boolean(report.applied));
  out.set("data", encode(report.data));
  if (report.image) {
    Value image = Value::object();
    image.set("path", Value::string(report.image->path));
    image.set("width", Value::number(static_cast<std::uint64_t>(report.image->width)));
    image.set("height", Value::number(static_cast<std::uint64_t>(report.image->height)));
    out.set("image", std::move(image));
  } else {
    out.set("image", Value());
  }
  return out;
}

std::optional<connection::FrameReport> decodeFrameReport(const Value& value) {
  const Value* frame = member(value, "frame");
  const Value* applied = member(value, "applied");
  const Value* data = member(value, "data");
  const Value* image = member(value, "image");
  if (frame == nullptr || applied == nullptr || !applied->isBool() || data == nullptr || image == nullptr) {
    return std::nullopt;
  }
  connection::FrameReport report;
  const auto f = decodeUInt64(*frame);
  auto d = decodeFrameData(*data);
  if (!f || !d) return std::nullopt;
  report.frame = *f;
  report.applied = applied->asBool();
  report.data = std::move(*d);
  if (!image->isNull()) {
    const Value* path = member(*image, "path");
    const Value* width = member(*image, "width");
    const Value* height = member(*image, "height");
    if (path == nullptr || !path->isString() || width == nullptr || height == nullptr) return std::nullopt;
    const auto w = decodeUInt64(*width);
    const auto h = decodeUInt64(*height);
    if (!w || !h || *w > UINT32_MAX || *h > UINT32_MAX) return std::nullopt;
    report.image = connection::CapturedImage{path->asString(), static_cast<std::uint32_t>(*w),
                                             static_cast<std::uint32_t>(*h)};
  }
  return report;
}

Value encode(const connection::DiagnosticBatch& batch) {
  Value entries = Value::array();
  for (const connection::Diagnostic& record : batch.entries) {
    Value r = Value::object();
    r.set("sequence", Value::number(record.sequence));
    r.set("severity", Value::string(std::string(connection::toString(record.severity))));
    r.set("message", Value::string(record.message));
    entries.push(std::move(r));
  }
  Value out = Value::object();
  out.set("entries", std::move(entries));
  out.set("latest", Value::number(batch.latest));
  out.set("dropped", Value::number(batch.dropped));
  return out;
}

std::optional<connection::DiagnosticBatch> decodeDiagnosticBatch(const Value& value) {
  const Value* entries = member(value, "entries");
  const Value* latest = member(value, "latest");
  const Value* dropped = member(value, "dropped");
  if (entries == nullptr || !entries->isArray() || latest == nullptr || dropped == nullptr) return std::nullopt;
  connection::DiagnosticBatch batch;
  for (const Value& r : entries->asArray()) {
    const Value* sequence = member(r, "sequence");
    const Value* severity = member(r, "severity");
    const Value* message = member(r, "message");
    if (sequence == nullptr || severity == nullptr || !severity->isString() || message == nullptr ||
        !message->isString()) {
      return std::nullopt;
    }
    const auto s = decodeUInt64(*sequence);
    std::optional<connection::DiagnosticSeverity> level;
    for (const connection::DiagnosticSeverity candidate : kSeverities) {
      if (severity->asString() == connection::toString(candidate)) level = candidate;
    }
    if (!s || !level) return std::nullopt;
    batch.entries.push_back(connection::Diagnostic{*s, *level, message->asString()});
  }
  const auto l = decodeUInt64(*latest);
  const auto d = decodeUInt64(*dropped);
  if (!l || !d) return std::nullopt;
  batch.latest = *l;
  batch.dropped = *d;
  return batch;
}

}  // namespace atlantis::remote::codec
