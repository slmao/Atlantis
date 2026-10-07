#include <atlantis/cli/command_layer.h>

#include <atlantis/connection/text.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <random>
#include <type_traits>
#include <utility>
#include <variant>

namespace atlantis::cli {

namespace access = atlantis::world::access;
namespace connection = atlantis::connection;
namespace json = atlantis::connection::json;
namespace text = atlantis::connection::text;
using atlantis::asset_system::EntityGuid;
using json::Value;
using schema::FieldDescriptor;
using schema::FieldFlags;
using schema::PrimitiveKind;
using schema::TypeDescriptor;
using schema::TypeId;
using schema::TypeKind;

std::string_view toString(ErrorCategory category) noexcept {
  switch (category) {
    case ErrorCategory::Text: return "text";
    case ErrorCategory::Usage: return "usage";
    case ErrorCategory::Refused: return "refused";
    case ErrorCategory::Connection: return "connection";
    case ErrorCategory::Runtime: return "runtime";
  }
  return "usage";
}

int exitCode(ErrorCategory category) noexcept {
  switch (category) {
    case ErrorCategory::Refused: return 1;
    case ErrorCategory::Text:
    case ErrorCategory::Usage: return 2;
    case ErrorCategory::Connection: return 3;
    case ErrorCategory::Runtime: return 4;
  }
  return 2;
}

std::vector<std::string_view> tokenize(std::string_view line) {
  std::vector<std::string_view> tokens;
  std::size_t i = 0;
  const auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
  while (i < line.size()) {
    while (i < line.size() && isSpace(line[i])) ++i;
    const std::size_t start = i;
    while (i < line.size() && !isSpace(line[i])) ++i;
    if (i > start) tokens.push_back(line.substr(start, i - start));
  }
  return tokens;
}

namespace {

// --- Formatting --------------------------------------------------------------

// %.9g, as Spec 0054's text forms print floats.
[[nodiscard]] std::string formatFloat(float value) {
  std::array<char, 32> buffer{};
  std::snprintf(buffer.data(), buffer.size(), "%.9g", static_cast<double>(value));
  return std::string(buffer.data());
}

[[nodiscard]] std::string formatId(std::uint64_t id) {
  std::array<char, 24> buffer{};
  std::snprintf(buffer.data(), buffer.size(), "0x%016llx", static_cast<unsigned long long>(id));
  return std::string(buffer.data());
}

[[nodiscard]] std::string_view kindName(TypeKind kind) {
  switch (kind) {
    case TypeKind::Primitive: return "primitive";
    case TypeKind::Struct: return "struct";
    case TypeKind::Enum: return "enum";
  }
  return "?";
}

[[nodiscard]] std::string_view primitiveName(PrimitiveKind kind) {
  switch (kind) {
    case PrimitiveKind::UInt64: return "uint64";
    case PrimitiveKind::Float32: return "float32";
    case PrimitiveKind::Vec3Float32: return "vec3";
    case PrimitiveKind::Vec4Float32: return "vec4";
    case PrimitiveKind::AssetGuid: return "assetguid";
    case PrimitiveKind::EntityGuid: return "entityguid";
  }
  return "?";
}

constexpr std::array<std::pair<FieldFlags, std::string_view>, 5> kFlagNames = {{
    {FieldFlags::Serializable, "serializable"},
    {FieldFlags::Editable, "editable"},
    {FieldFlags::AssetReference, "assetref"},
    {FieldFlags::EntityReference, "entityref"},
    {FieldFlags::Optional, "optional"},
}};

[[nodiscard]] std::string flagNames(FieldFlags flags) {
  std::string out;
  for (const auto& [flag, name] : kFlagNames) {
    if (!schema::hasFlags(flags, flag)) continue;
    if (!out.empty()) out += ',';
    out += name;
  }
  return out.empty() ? "-" : out;
}

// Spec 0055 ruling Q3: a type's identity.
[[nodiscard]] Value typeIdentity(const TypeDescriptor& type) {
  Value out = Value::object();
  out.set("name", Value::string(std::string(text::shortName(type.name))));
  out.set("qualified", Value::string(std::string(type.name)));
  out.set("typeId", Value::string(formatId(type.id.value)));
  return out;
}

[[nodiscard]] Value floatValue(float value) {
  if (!std::isfinite(value)) return Value::string(std::isnan(value) ? "nan" : (value > 0 ? "inf" : "-inf"));
  return Value::number(value);
}

// Spec 0055 ruling Q3's value table.
[[nodiscard]] Value jsonValue(std::span<const TypeDescriptor> schema, const FieldDescriptor& leaf,
                              const access::PropertyValue& value) {
  return std::visit(
      [&](const auto& v) -> Value {
        using V = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<V, std::uint64_t>) {
          return Value::string(std::to_string(v));
        } else if constexpr (std::is_same_v<V, float>) {
          return floatValue(v);
        } else if constexpr (std::is_same_v<V, std::array<float, 3>> || std::is_same_v<V, std::array<float, 4>>) {
          Value out = Value::array();
          for (const float component : v) out.push(floatValue(component));
          return out;
        } else if constexpr (std::is_same_v<V, atlantis::asset_system::AssetGuid> || std::is_same_v<V, EntityGuid>) {
          return Value::string(v == V{} ? std::string("00000000-0000-0000-0000-000000000000")
                                        : atlantis::asset_system::toString(v));
        } else if constexpr (std::is_same_v<V, access::EnumValue>) {
          Value out = Value::object();
          std::string name;
          if (const TypeDescriptor* enumType = text::findType(schema, leaf.type)) {
            for (const auto& constant : enumType->constants) {
              if (constant.value == v.value) name = std::string(constant.name);
            }
          }
          out.set("name", name.empty() ? Value() : Value::string(name));
          out.set("value", Value::number(v.value));
          return out;
        } else {
          static_assert(std::is_same_v<V, access::Absent>);
          return Value();
        }
      },
      value);
}

// Bitwise equality (Plan 0055 P9, J5: float equality is bitwise after parse).
[[nodiscard]] bool sameBits(const access::PropertyValue& a, const access::PropertyValue& b) {
  if (a.index() != b.index()) return false;
  return std::visit(
      [&](const auto& x) {
        using V = std::decay_t<decltype(x)>;
        const V& y = std::get<V>(b);
        if constexpr (std::is_same_v<V, float> || std::is_same_v<V, std::array<float, 3>> ||
                      std::is_same_v<V, std::array<float, 4>>) {
          return std::memcmp(&x, &y, sizeof(V)) == 0;
        } else {
          return x == y;
        }
      },
      a);
}

struct Leaf {
  std::string path;  // "Light.intensity"
  const FieldDescriptor* field = nullptr;
};

[[nodiscard]] std::optional<Leaf> leafOf(std::span<const TypeDescriptor> schema, TypeId component,
                                         schema::FieldId field) {
  for (const text::LeafPath& leaf : text::leavesOf(schema, component)) {
    if (leaf.leaf->id == field) return Leaf{leaf.path, leaf.leaf};
  }
  return std::nullopt;
}

[[nodiscard]] std::string typeName(std::span<const TypeDescriptor> schema, TypeId id) {
  const TypeDescriptor* type = text::findType(schema, id);
  return type ? std::string(text::shortName(type->name)) : "?";
}

// --- Errors -------------------------------------------------------------------

[[nodiscard]] Value subject(std::initializer_list<std::pair<const char*, Value>> facts) {
  Value out = Value::object();
  for (const auto& [key, value] : facts) out.set(key, value);
  return out;
}

[[nodiscard]] CliError usageError(std::string_view usage) {
  return CliError{"Usage", ErrorCategory::Usage, std::string(usage),
                  subject({{"usage", Value::string(std::string(usage))}}), "error: usage: " + std::string(usage)};
}

[[nodiscard]] CliError textError(text::TextError why, std::string_view token) {
  return CliError{std::string(text::toString(why)), ErrorCategory::Text,
                  std::string(text::toString(why)) + " in '" + std::string(token) + "'",
                  subject({{"token", Value::string(std::string(token))}}),
                  "error: " + std::string(text::toString(why)) + " '" + std::string(token) + "'"};
}

[[nodiscard]] CliError refusedError(access::AccessError why, Value facts) {
  return CliError{std::string(access::toString(why)), ErrorCategory::Refused,
                  "the Runtime World refused it: " + std::string(access::toString(why)), std::move(facts),
                  "refused " + std::string(access::toString(why))};
}

[[nodiscard]] CliError plainError(std::string code, ErrorCategory category, std::string message, Value facts = {}) {
  std::string line = "error: " + message;
  return CliError{std::move(code), category, std::move(message), facts.isNull() ? Value::object() : std::move(facts),
                  std::move(line)};
}

[[nodiscard]] Reply failed(std::string command, CliError error) {
  Reply reply;
  reply.command = std::move(command);
  reply.outcome = error.category == ErrorCategory::Refused ? Outcome::Refused : Outcome::Error;
  reply.text = error.text + "\n";
  reply.error = std::move(error);
  return reply;
}

[[nodiscard]] Reply done(std::string command, std::string human, Value result) {
  Reply reply;
  reply.command = std::move(command);
  reply.outcome = Outcome::Done;
  reply.text = std::move(human);
  reply.result = std::move(result);
  return reply;
}

class SequentialQueries final : public QueryBatch {
 public:
  explicit SequentialQueries(connection::RuntimeConnection& connection) : connection_(connection) {}
  std::vector<atlantis::Result<std::vector<TypeId>, access::AccessError>> listComponents(
      std::span<const EntityGuid> entities) override {
    std::vector<atlantis::Result<std::vector<TypeId>, access::AccessError>> out;
    for (const EntityGuid& entity : entities) out.push_back(connection_.listComponents(entity));
    return out;
  }
  std::vector<atlantis::Result<access::PropertyValue, access::AccessError>> getProperties(
      std::span<const access::PropertyAddress> addresses) override {
    std::vector<atlantis::Result<access::PropertyValue, access::AccessError>> out;
    for (const access::PropertyAddress& address : addresses) out.push_back(connection_.getProperty(address));
    return out;
  }

 private:
  connection::RuntimeConnection& connection_;
};

// RFC 9562 version 4: 122 random bits.
[[nodiscard]] EntityGuid randomGuid() {
  std::random_device device;
  EntityGuid guid;
  for (std::size_t i = 0; i < guid.bytes.size(); i += 4) {
    const std::uint32_t bits = device();
    for (std::size_t b = 0; b < 4; ++b) guid.bytes[i + b] = static_cast<std::byte>((bits >> (b * 8)) & 0xFF);
  }
  guid.bytes[6] = static_cast<std::byte>((static_cast<unsigned>(guid.bytes[6]) & 0x0F) | 0x40);
  guid.bytes[8] = static_cast<std::byte>((static_cast<unsigned>(guid.bytes[8]) & 0x3F) | 0x80);
  return guid;
}

// --- Events -------------------------------------------------------------------

// Whether `event` is the one `command` produces when applied.
[[nodiscard]] bool produces(const access::Command& command, const access::Event& event) {
  return std::visit(
      [&](const auto& c) {
        using C = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<C, access::CreateEntity>) {
          const auto* e = std::get_if<access::EntityCreated>(&event);
          return e != nullptr && e->entity == c.entity;
        } else if constexpr (std::is_same_v<C, access::DestroyEntity>) {
          const auto* e = std::get_if<access::EntityDestroyed>(&event);
          return e != nullptr && e->entity == c.entity;
        } else if constexpr (std::is_same_v<C, access::AddComponent>) {
          const auto* e = std::get_if<access::ComponentAdded>(&event);
          return e != nullptr && e->entity == c.entity && e->component == c.component;
        } else if constexpr (std::is_same_v<C, access::RemoveComponent>) {
          const auto* e = std::get_if<access::ComponentRemoved>(&event);
          return e != nullptr && e->entity == c.entity && e->component == c.component;
        } else {
          const auto* e = std::get_if<access::PropertyChanged>(&event);
          return e != nullptr && e->address == c.address && sameBits(e->value, c.value);
        }
      },
      command);
}

[[nodiscard]] std::string eventText(std::span<const TypeDescriptor> schema, const access::Event& event) {
  return std::visit(
      [&](const auto& e) -> std::string {
        using E = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<E, access::EntityCreated>) {
          return "created " + text::formatEntity(e.entity);
        } else if constexpr (std::is_same_v<E, access::EntityDestroyed>) {
          return "destroyed " + text::formatEntity(e.entity);
        } else if constexpr (std::is_same_v<E, access::ComponentAdded>) {
          return "added " + text::formatEntity(e.entity) + " " + typeName(schema, e.component);
        } else if constexpr (std::is_same_v<E, access::ComponentRemoved>) {
          return "removed " + text::formatEntity(e.entity) + " " + typeName(schema, e.component);
        } else {
          const auto leaf = leafOf(schema, e.address.component, e.address.field);
          if (!leaf) return text::formatEntity(e.address.entity) + " ? = ?";
          return text::formatEntity(e.address.entity) + " " + leaf->path + " = " +
                 text::formatValue(schema, *leaf->field, e.value);
        }
      },
      event);
}

[[nodiscard]] Value eventJson(std::span<const TypeDescriptor> schema, const access::Event& event) {
  Value out = Value::object();
  std::visit(
      [&](const auto& e) {
        using E = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<E, access::EntityCreated> || std::is_same_v<E, access::EntityDestroyed>) {
          out.set("kind", Value::string(std::is_same_v<E, access::EntityCreated> ? "EntityCreated" : "EntityDestroyed"));
          out.set("guid", Value::string(text::formatEntity(e.entity)));
        } else if constexpr (std::is_same_v<E, access::ComponentAdded> || std::is_same_v<E, access::ComponentRemoved>) {
          out.set("kind",
                  Value::string(std::is_same_v<E, access::ComponentAdded> ? "ComponentAdded" : "ComponentRemoved"));
          out.set("guid", Value::string(text::formatEntity(e.entity)));
          const TypeDescriptor* type = text::findType(schema, e.component);
          out.set("component", type ? typeIdentity(*type) : Value::string(formatId(e.component.value)));
        } else {
          out.set("kind", Value::string("PropertyChanged"));
          out.set("guid", Value::string(text::formatEntity(e.address.entity)));
          const auto leaf = leafOf(schema, e.address.component, e.address.field);
          out.set("path", leaf ? Value::string(leaf->path) : Value());
          out.set("value", leaf ? jsonValue(schema, *leaf->field, e.value) : Value());
        }
      },
      event);
  return out;
}

[[nodiscard]] connection::EventFilter filterFor(const std::vector<access::Command>& commands) {
  if (commands.size() != 1) return connection::EventFilter{};
  return std::visit(
      [](const auto& c) {
        using C = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<C, access::SetProperty>) {
          return connection::EventFilter{connection::EventKindSet::only(connection::EventKind::PropertyChanged),
                                         c.address.entity, c.address.component};
        } else if constexpr (std::is_same_v<C, access::AddComponent> || std::is_same_v<C, access::RemoveComponent>) {
          return connection::EventFilter{connection::EventKindSet::all(), c.entity, c.component};
        } else {
          return connection::EventFilter{connection::EventKindSet::all(), c.entity, std::nullopt};
        }
      },
      commands.front());
}

}  // namespace

// --- The pending write or step --------------------------------------------------

struct CommandLayer::Pending {
  enum class Kind { Write, Transaction, Step };
  Kind kind = Kind::Write;
  std::string command;  // tree path
  // Writes
  std::vector<access::Command> commands;
  access::TransactionTicket tickets;
  connection::SubscriptionId subscription;
  std::vector<access::Event> seen;
  // Steps: filled by the control's completion
  std::shared_ptr<std::optional<atlantis::Result<connection::FrameReport, connection::ControlError>>> step;
  bool capture = false;
};

namespace {

[[nodiscard]] std::string stepText(const connection::FrameReport& report, bool capture) {
  std::string out = "frame " + std::to_string(report.frame) + "\n";
  if (!capture) return out;
  const auto vec = [](const std::array<float, 3>& v) {
    return formatFloat(v[0]) + " " + formatFloat(v[1]) + " " + formatFloat(v[2]);
  };
  for (const auto& light : report.data.directionalLights) {
    out += "directional direction " + vec(light.direction) + " color " + vec(light.color) + " intensity " +
           formatFloat(light.intensity) + "\n";
  }
  for (const auto& light : report.data.pointLights) {
    out += "point position " + vec(light.position) + " color " + vec(light.color) + " intensity " +
           formatFloat(light.intensity) + " range " + formatFloat(light.range) + "\n";
  }
  out += "draw items " + std::to_string(report.data.drawItemCount) + "\n";
  if (report.image) {
    out += "image " + report.image->path + " " + std::to_string(report.image->width) + "x" +
           std::to_string(report.image->height) + "\n";
  }
  return out;
}

[[nodiscard]] Value floats(std::span<const float> values) {
  Value out = Value::array();
  for (const float v : values) out.push(floatValue(v));
  return out;
}

[[nodiscard]] Value stepJson(const connection::FrameReport& report, bool capture) {
  Value out = Value::object();
  out.set("frame", Value::number(report.frame));
  out.set("applied", Value::boolean(report.applied));
  if (!capture) return out;
  Value directional = Value::array();
  for (const auto& light : report.data.directionalLights) {
    Value l = Value::object();
    l.set("direction", floats(light.direction));
    l.set("color", floats(light.color));
    l.set("intensity", floatValue(light.intensity));
    directional.push(std::move(l));
  }
  Value points = Value::array();
  for (const auto& light : report.data.pointLights) {
    Value l = Value::object();
    l.set("position", floats(light.position));
    l.set("color", floats(light.color));
    l.set("intensity", floatValue(light.intensity));
    l.set("range", floatValue(light.range));
    points.push(std::move(l));
  }
  Value frameData = Value::object();
  frameData.set("directionalLights", std::move(directional));
  frameData.set("pointLights", std::move(points));
  frameData.set("view", floats(report.data.view));
  frameData.set("projection", floats(report.data.projection));
  frameData.set("drawItemCount", Value::number(report.data.drawItemCount));
  Value captureValue = Value::object();
  captureValue.set("frameData", std::move(frameData));
  if (report.image) {
    Value image = Value::object();
    image.set("path", Value::string(report.image->path));
    image.set("width", Value::number(static_cast<std::uint64_t>(report.image->width)));
    image.set("height", Value::number(static_cast<std::uint64_t>(report.image->height)));
    captureValue.set("image", std::move(image));
  }
  out.set("capture", std::move(captureValue));
  return out;
}

[[nodiscard]] CliError controlError(connection::ControlError why) {
  return plainError(std::string(connection::toString(why)), ErrorCategory::Runtime,
                    "the Runtime could not complete the step: " + std::string(connection::toString(why)));
}

}  // namespace

CommandLayer::CommandLayer(connection::RuntimeConnection& connection, connection::RuntimeControl* control,
                           QueryBatch* batch)
    : connection_(connection),
      control_(control),
      batch_(batch),
      sequential_(std::make_unique<SequentialQueries>(connection)),
      guidSource_(&randomGuid) {
  if (batch_ == nullptr) batch_ = sequential_.get();
}

CommandLayer::~CommandLayer() { abandon(); }

bool CommandLayer::hasPending() const noexcept { return pending_ != nullptr; }

void CommandLayer::abandon() {
  if (pending_ && pending_->kind != Pending::Kind::Step) (void)connection_.unsubscribe(pending_->subscription);
  pending_.reset();
}

void CommandLayer::setGuidSource(std::function<EntityGuid()> source) { guidSource_ = std::move(source); }

atlantis::Result<std::vector<access::Command>, CliError> CommandLayer::parseWrite(
    std::span<const std::string_view> t) {
  using ResultT = atlantis::Result<std::vector<access::Command>, CliError>;
  const std::span<const TypeDescriptor> schema = connection_.schema();
  const auto entityAt = [&](std::size_t i) { return text::parseEntity(t[i]); };
  const auto componentAt = [&](std::size_t i) -> atlantis::Result<TypeId, CliError> {
    const TypeDescriptor* type = text::findType(schema, t[i]);
    if (type == nullptr || type->kind != TypeKind::Struct) {
      return atlantis::Result<TypeId, CliError>::Err(textError(text::TextError::UnknownType, t[i]));
    }
    return atlantis::Result<TypeId, CliError>::Ok(type->id);
  };
  if (t.size() >= 2 && t[0] == "entity" && t[1] == "create") {
    std::optional<EntityGuid> guid;
    std::vector<TypeId> components;
    for (std::size_t i = 2; i < t.size(); ++i) {
      if (t[i] == "--component") {
        if (i + 1 >= t.size()) return ResultT::Err(usageError("entity create [<guid>] [--component <Type>]..."));
        auto component = componentAt(++i);
        if (component.isErr()) return ResultT::Err(component.error());
        components.push_back(component.value());
      } else if (!guid && !t[i].starts_with("--")) {
        const auto parsed = entityAt(i);
        if (parsed.isErr()) return ResultT::Err(textError(parsed.error(), t[i]));
        guid = parsed.value();
      } else {
        return ResultT::Err(usageError("entity create [<guid>] [--component <Type>]..."));
      }
    }
    const EntityGuid entity = guid.value_or(guidSource_());
    std::vector<access::Command> commands{access::CreateEntity{entity}};
    for (const TypeId component : components) commands.push_back(access::AddComponent{entity, component});
    return ResultT::Ok(std::move(commands));
  }
  if (t.size() >= 2 && t[0] == "entity" && t[1] == "destroy") {
    if (t.size() != 3) return ResultT::Err(usageError("entity destroy <guid>"));
    const auto entity = entityAt(2);
    if (entity.isErr()) return ResultT::Err(textError(entity.error(), t[2]));
    return ResultT::Ok(std::vector<access::Command>{access::DestroyEntity{entity.value()}});
  }
  if (t.size() >= 2 && t[0] == "component" && (t[1] == "add" || t[1] == "remove")) {
    if (t.size() != 4) return ResultT::Err(usageError("component " + std::string(t[1]) + " <guid> <Type>"));
    const auto entity = entityAt(2);
    if (entity.isErr()) return ResultT::Err(textError(entity.error(), t[2]));
    auto component = componentAt(3);
    if (component.isErr()) return ResultT::Err(component.error());
    if (t[1] == "add") {
      return ResultT::Ok(std::vector<access::Command>{access::AddComponent{entity.value(), component.value()}});
    }
    return ResultT::Ok(std::vector<access::Command>{access::RemoveComponent{entity.value(), component.value()}});
  }
  if (t.size() >= 2 && ((t[0] == "property" && t[1] == "set") || (t[0] == "entity" && t[1] == "set"))) {
    if (t.size() < 5) {
      return ResultT::Err(usageError(std::string(t[0]) + " set <guid> <Type>.<field> <value...>"));
    }
    const auto entity = entityAt(2);
    if (entity.isErr()) return ResultT::Err(textError(entity.error(), t[2]));
    const auto path = text::parsePath(schema, t[3]);
    if (path.isErr()) return ResultT::Err(textError(path.error(), t[3]));
    const std::span<const std::string_view> valueTokens = t.subspan(4);
    const auto value = text::parseValue(schema, *path.value().leaf, valueTokens);
    if (value.isErr()) return ResultT::Err(textError(value.error(), t[4]));
    return ResultT::Ok(std::vector<access::Command>{access::SetProperty{
        {entity.value(), path.value().component, path.value().leaf->id}, value.value()}});
  }
  return ResultT::Err(plainError("NotAWrite", ErrorCategory::Usage,
                                 "not a write command: entity create|destroy, component add|remove, property set"));
}

Reply CommandLayer::run(std::span<const std::string_view> t, std::string_view line) {
  const std::span<const TypeDescriptor> schema = connection_.schema();
  if (t.empty()) return failed("", plainError("EmptyCommand", ErrorCategory::Usage, "empty command"));
  if (pending_) {
    return failed("", plainError("WritePending", ErrorCategory::Usage, "the previous `entity set` has no outcome yet"));
  }
  const auto is = [&](std::string_view a, std::string_view b) { return t.size() >= 2 && t[0] == a && t[1] == b; };

  // schema list | schema inspect <Type>
  if (t.size() == 2 && is("schema", "list")) {
    std::string human;
    Value result = Value::array();
    for (const TypeDescriptor& type : schema) {
      human += std::string(text::shortName(type.name)) + " " + std::string(kindName(type.kind)) + "\n";
      result.push(typeIdentity(type));
    }
    return done("schema list", std::move(human), std::move(result));
  }
  if (is("schema", "inspect")) {
    if (t.size() != 3) return failed("schema inspect", usageError("schema inspect <Type>"));
    const TypeDescriptor* type = text::findType(schema, t[2]);
    if (type == nullptr) return failed("schema inspect", textError(text::TextError::UnknownType, t[2]));
    std::string human = std::string(text::shortName(type->name)) + " " + std::string(kindName(type->kind)) + " (" +
                        std::string(type->name) + ") v" + std::to_string(type->schemaVersion) + "\n";
    Value result = typeIdentity(*type);
    result.set("kind", Value::string(std::string(kindName(type->kind))));
    result.set("version", Value::number(static_cast<std::uint64_t>(type->schemaVersion)));
    Value fields = Value::array();
    for (const FieldDescriptor& field : type->fields) {
      human += "  " + std::string(field.name) + " ";
      Value f = Value::object();
      f.set("name", Value::string(std::string(field.name)));
      if (field.kind == TypeKind::Primitive) {
        human += std::string(primitiveName(field.primitive));
        f.set("kind", Value::string(std::string(primitiveName(field.primitive))));
      } else {
        const TypeDescriptor* referenced = text::findType(schema, field.type);
        human += std::string(kindName(field.kind)) + " " +
                 std::string(referenced ? text::shortName(referenced->name) : "?");
        f.set("kind", Value::string(std::string(kindName(field.kind))));
        f.set("type", referenced ? typeIdentity(*referenced) : Value());
      }
      human += " " + flagNames(field.flags) + "\n";
      Value flags = Value::array();
      for (const auto& [flag, name] : kFlagNames) {
        if (schema::hasFlags(field.flags, flag)) flags.push(Value::string(std::string(name)));
      }
      f.set("flags", std::move(flags));
      fields.push(std::move(f));
    }
    Value constants = Value::array();
    for (const auto& constant : type->constants) {
      human += "  " + std::string(constant.name) + " = " + std::to_string(constant.value) + "\n";
      Value c = Value::object();
      c.set("name", Value::string(std::string(constant.name)));
      c.set("value", Value::number(constant.value));
      constants.push(std::move(c));
    }
    if (type->kind == TypeKind::Enum) {
      result.set("constants", std::move(constants));
    } else {
      result.set("fields", std::move(fields));
    }
    return done("schema inspect", std::move(human), std::move(result));
  }

  // world list | world inspect
  const auto statusOrNull = [&]() -> std::optional<connection::RuntimeStatus> {
    if (control_ == nullptr) return std::nullopt;
    return control_->status();
  };
  if (t.size() == 2 && is("world", "list")) {
    const std::vector<EntityGuid> entities = connection_.listEntities();
    const auto status = statusOrNull();
    const std::string scene = status ? atlantis::asset_system::toString(status->scene) : std::string("-");
    Value world = Value::object();
    world.set("scene", status ? Value::string(scene) : Value());
    world.set("entities", Value::number(static_cast<std::uint64_t>(entities.size())));
    Value result = Value::array();
    result.push(std::move(world));
    return done("world list", scene + " " + std::to_string(entities.size()) + "\n", std::move(result));
  }
  if (t.size() == 2 && is("world", "inspect")) {
    const std::vector<EntityGuid> entities = connection_.listEntities();
    const auto components = batch_->listComponents(entities);
    std::vector<std::uint64_t> counts(schema.size(), 0);
    for (const auto& list : components) {
      if (list.isErr()) continue;
      for (const TypeId component : list.value()) {
        for (std::size_t i = 0; i < schema.size(); ++i) {
          if (schema[i].id == component) ++counts[i];
        }
      }
    }
    const auto status = statusOrNull();
    std::string human = "scene " + (status ? atlantis::asset_system::toString(status->scene) : std::string("-")) +
                        "\nentities " + std::to_string(entities.size()) + "\ncomponents ";
    Value componentCounts = Value::object();
    bool first = true;
    for (std::size_t i = 0; i < schema.size(); ++i) {
      if (counts[i] == 0) continue;
      const std::string name(text::shortName(schema[i].name));
      human += (first ? "" : ",") + name + "=" + std::to_string(counts[i]);
      first = false;
      componentCounts.set(name, Value::number(counts[i]));
    }
    human += (first ? "-" : "") + std::string("\npaused ") +
             (status ? (status->paused ? "true" : "false") : "-") + "\nframe " +
             (status ? std::to_string(status->frame) : std::string("-")) + "\n";
    Value result = Value::object();
    result.set("scene", status ? Value::string(atlantis::asset_system::toString(status->scene)) : Value());
    result.set("entities", Value::number(static_cast<std::uint64_t>(entities.size())));
    result.set("components", std::move(componentCounts));
    result.set("paused", status ? Value::boolean(status->paused) : Value());
    result.set("frame", status ? Value::number(status->frame) : Value());
    return done("world inspect", std::move(human), std::move(result));
  }

  // entity list [--with <Type>]... [--where <Type>.<field>=<value>]...  (alias: world entities)
  if ((t.size() == 2 && is("world", "entities")) || is("entity", "list")) {
    std::vector<TypeId> with;
    struct Where {
      access::PropertyAddress address;  // entity filled per candidate
      access::PropertyValue value;
    };
    std::vector<Where> where;
    for (std::size_t i = 2; i < t.size(); ++i) {
      if ((t[i] != "--with" && t[i] != "--where") || i + 1 >= t.size()) {
        return failed("entity list", usageError("entity list [--with <Type>]... [--where <Type>.<field>=<value>]..."));
      }
      const std::string_view argument = t[++i];
      if (t[i - 1] == "--with") {
        const TypeDescriptor* type = text::findType(schema, argument);
        if (type == nullptr || type->kind != TypeKind::Struct) {
          return failed("entity list", textError(text::TextError::UnknownType, argument));
        }
        with.push_back(type->id);
        continue;
      }
      const auto equals = argument.find('=');
      if (equals == std::string_view::npos) {
        return failed("entity list", usageError("entity list [--with <Type>]... [--where <Type>.<field>=<value>]..."));
      }
      const std::string_view pathText = argument.substr(0, equals);
      const std::string_view valueText = argument.substr(equals + 1);
      const auto path = text::parsePath(schema, pathText);
      if (path.isErr()) return failed("entity list", textError(path.error(), pathText));
      std::vector<std::string_view> valueTokens;
      std::size_t start = 0;
      while (true) {  // J5: vectors are comma-separated
        const auto comma = valueText.find(',', start);
        valueTokens.push_back(valueText.substr(start, comma == std::string_view::npos ? std::string_view::npos
                                                                                      : comma - start));
        if (comma == std::string_view::npos) break;
        start = comma + 1;
      }
      const auto value = text::parseValue(schema, *path.value().leaf, valueTokens);
      if (value.isErr()) return failed("entity list", textError(value.error(), valueText));
      where.push_back(Where{{EntityGuid{}, path.value().component, path.value().leaf->id}, value.value()});
    }
    const std::vector<EntityGuid> entities = connection_.listEntities();
    const auto components = batch_->listComponents(entities);
    // Candidates: alive, with every --with type and every --where component.
    std::vector<std::size_t> candidates;
    for (std::size_t e = 0; e < entities.size(); ++e) {
      if (components[e].isErr()) continue;
      const auto& has = components[e].value();
      const auto contains = [&](TypeId id) { return std::find(has.begin(), has.end(), id) != has.end(); };
      bool keep = std::all_of(with.begin(), with.end(), contains);
      for (const Where& w : where) keep = keep && contains(w.address.component);
      if (keep) candidates.push_back(e);
    }
    if (!where.empty()) {
      std::vector<access::PropertyAddress> addresses;
      for (const std::size_t e : candidates) {
        for (const Where& w : where) addresses.push_back({entities[e], w.address.component, w.address.field});
      }
      const auto values = batch_->getProperties(addresses);
      std::vector<std::size_t> matched;
      for (std::size_t c = 0; c < candidates.size(); ++c) {
        bool keep = true;
        for (std::size_t w = 0; w < where.size(); ++w) {
          const auto& value = values[c * where.size() + w];
          keep = keep && value.isOk() && sameBits(value.value(), where[w].value);
        }
        if (keep) matched.push_back(candidates[c]);
      }
      candidates = std::move(matched);
    }
    std::string human;
    Value result = Value::array();
    for (const std::size_t e : candidates) {
      std::string names;
      Value componentNames = Value::array();
      for (const TypeId component : components[e].value()) {
        const std::string name = typeName(schema, component);
        if (!names.empty()) names += ',';
        names += name;
        componentNames.push(Value::string(name));
      }
      human += text::formatEntity(entities[e]) + " " + (names.empty() ? "-" : names) + "\n";
      Value entry = Value::object();
      entry.set("guid", Value::string(text::formatEntity(entities[e])));
      entry.set("components", std::move(componentNames));
      result.push(std::move(entry));
    }
    return done("entity list", std::move(human), std::move(result));
  }

  // entity inspect <guid>
  if (is("entity", "inspect")) {
    if (t.size() != 3) return failed("entity inspect", usageError("entity inspect <guid>"));
    const auto entity = text::parseEntity(t[2]);
    if (entity.isErr()) return failed("entity inspect", textError(entity.error(), t[2]));
    const auto components = connection_.listComponents(entity.value());
    if (components.isErr()) {
      return failed("entity inspect", refusedError(components.error(), subject({{"guid", Value::string(std::string(t[2]))}})));
    }
    std::vector<access::PropertyAddress> addresses;
    std::vector<std::pair<TypeId, text::LeafPath>> leaves;
    for (const TypeId component : components.value()) {
      for (text::LeafPath& leaf : text::leavesOf(schema, component)) {
        addresses.push_back({entity.value(), component, leaf.leaf->id});
        leaves.emplace_back(component, std::move(leaf));
      }
    }
    const auto values = batch_->getProperties(addresses);
    std::string human = text::formatEntity(entity.value()) + "\n";
    Value componentValues = Value::object();
    for (std::size_t i = 0; i < leaves.size(); ++i) {
      const auto& [component, leaf] = leaves[i];
      human += "  " + leaf.path + " = ";
      const std::string name = typeName(schema, component);
      if (componentValues.find(name) == nullptr) componentValues.set(name, Value::object());
      Value fieldValue;
      if (values[i].isOk()) {
        human += text::formatValue(schema, *leaf.leaf, values[i].value());
        fieldValue = jsonValue(schema, *leaf.leaf, values[i].value());
      } else {
        human += "(refused " + std::string(access::toString(values[i].error())) + ")";
      }
      human += "\n";
      Value updated = *componentValues.find(name);
      updated.set(leaf.path.substr(name.size() + 1), std::move(fieldValue));
      componentValues.set(name, std::move(updated));
    }
    Value result = Value::object();
    result.set("guid", Value::string(text::formatEntity(entity.value())));
    result.set("components", std::move(componentValues));
    return done("entity inspect", std::move(human), std::move(result));
  }

  // property get <guid> <Type>.<field>  (alias: entity get)
  if (is("property", "get") || is("entity", "get")) {
    if (t.size() != 4) return failed("property get", usageError(std::string(t[0]) + " get <guid> <Type>.<field>"));
    const auto entity = text::parseEntity(t[2]);
    if (entity.isErr()) return failed("property get", textError(entity.error(), t[2]));
    const auto path = text::parsePath(schema, t[3]);
    if (path.isErr()) return failed("property get", textError(path.error(), t[3]));
    const auto value = connection_.getProperty({entity.value(), path.value().component, path.value().leaf->id});
    const auto leaf = leafOf(schema, path.value().component, path.value().leaf->id);
    const std::string canonical = leaf ? leaf->path : std::string(t[3]);
    if (value.isErr()) {
      return failed("property get", refusedError(value.error(), subject({{"guid", Value::string(std::string(t[2]))},
                                                                          {"path", Value::string(canonical)}})));
    }
    Value result = Value::object();
    result.set("guid", Value::string(text::formatEntity(entity.value())));
    result.set("path", Value::string(canonical));
    result.set("value", jsonValue(schema, *path.value().leaf, value.value()));
    return done("property get", text::formatValue(schema, *path.value().leaf, value.value()) + "\n", std::move(result));
  }

  // Writes (Spec 0055 ruling Q4, T-a): one ticketed submission each.
  const bool isWrite = is("entity", "create") || is("entity", "destroy") || is("component", "add") ||
                       is("component", "remove") || is("property", "set") || is("entity", "set");
  if (isWrite) {
    std::string command = is("entity", "set") ? "property set" : std::string(t[0]) + " " + std::string(t[1]);
    auto parsed = parseWrite(t);
    if (parsed.isErr()) return failed(command, parsed.error());
    std::vector<access::Command> commands = std::move(parsed.value());
    auto pending = std::make_unique<Pending>();
    pending->command = command;
    pending->subscription = connection_.subscribe(filterFor(commands));
    if (commands.size() == 1) {
      const access::CommandTicket ticket = connection_.submit(commands.front());
      pending->tickets = access::TransactionTicket{ticket, 1};
    } else {
      pending->tickets = connection_.submitTransaction(commands);
    }
    pending->commands = std::move(commands);
    pending_ = std::move(pending);
    Reply reply;
    reply.command = std::move(command);
    reply.outcome = Outcome::Submitted;
    reply.result = Value::object();
    reply.result.set("ticket", Value::number(pending_->tickets.first.value));
    if (pending_->tickets.count > 1) reply.result.set("count", Value::number(pending_->tickets.count));
    return reply;
  }

  // runtime pause | resume | step [--frames <n>] [--capture [<path>]]
  if (t.size() >= 2 && t[0] == "runtime" && (t[1] == "pause" || t[1] == "resume" || t[1] == "step")) {
    const std::string command = "runtime " + std::string(t[1]);
    if (control_ == nullptr) {
      return failed(command, plainError("NoRuntimeControl", ErrorCategory::Runtime,
                                        "this connection carries no Runtime control"));
    }
    if (t[1] != "step") {
      if (t.size() != 2) return failed(command, usageError(command));
      if (t[1] == "pause") {
        control_->pause();
      } else {
        control_->resume();
      }
      Value result = Value::object();
      result.set("paused", Value::boolean(t[1] == "pause"));
      return done(command, t[1] == "pause" ? "paused\n" : "running\n", std::move(result));
    }
    connection::StepRequest request;
    bool capture = false;
    for (std::size_t i = 2; i < t.size(); ++i) {
      if (t[i] == "--frames" && i + 1 < t.size()) {
        const std::string_view n = t[++i];
        std::uint64_t frames = 0;
        bool valid = !n.empty() && n.size() <= 9;
        for (const char c : n) {
          valid = valid && c >= '0' && c <= '9';
          if (valid) frames = frames * 10 + static_cast<std::uint64_t>(c - '0');
        }
        if (!valid || frames == 0) return failed(command, textError(text::TextError::MalformedNumber, n));
        request.frames = static_cast<std::uint32_t>(frames);
      } else if (t[i] == "--capture") {
        capture = true;
        if (i + 1 < t.size() && !t[i + 1].starts_with("--")) {
          // The Runtime writes the image, in its own working directory:
          // the path goes as an absolute one. A directory gets capture.png.
          std::error_code error;
          std::filesystem::path path = std::filesystem::absolute(std::filesystem::path(t[++i]), error);
          if (std::filesystem::is_directory(path, error)) path /= "capture.png";
          request.imagePath = path.lexically_normal().string();
        }
      } else {
        return failed(command, usageError("runtime step [--frames <n>] [--capture [<path>]]"));
      }
    }
    auto pending = std::make_unique<Pending>();
    pending->kind = Pending::Kind::Step;
    pending->command = command;
    pending->capture = capture;
    pending->step = std::make_shared<std::optional<atlantis::Result<connection::FrameReport, connection::ControlError>>>();
    control_->step(request, [slot = pending->step](atlantis::Result<connection::FrameReport, connection::ControlError> r) {
      *slot = std::move(r);
    });
    pending_ = std::move(pending);
    if (pending_->step->has_value()) return poll();  // a remote control completes the step before returning
    Reply reply;
    reply.command = command;
    reply.outcome = Outcome::Submitted;
    return reply;
  }

  return failed("", plainError("UnknownCommand", ErrorCategory::Usage, "unknown command '" + std::string(line) + "'"));
}

Reply CommandLayer::submitTransaction(std::vector<access::Command> commands) {
  auto pending = std::make_unique<Pending>();
  pending->kind = Pending::Kind::Transaction;
  pending->command = "tx";
  pending->subscription = connection_.subscribe(connection::EventFilter{});
  pending->tickets = connection_.submitTransaction(commands);
  Value ticket = Value::object();
  ticket.set("first", Value::number(pending->tickets.first.value));
  ticket.set("count", Value::number(pending->tickets.count));
  pending->commands = std::move(commands);
  pending_ = std::move(pending);
  Reply reply;
  reply.command = "tx";
  reply.outcome = Outcome::Submitted;
  reply.result = Value::object();
  reply.result.set("ticket", std::move(ticket));
  return reply;
}

Reply CommandLayer::poll() {
  if (!pending_) return done("", "", Value());
  Pending& pending = *pending_;
  const std::span<const TypeDescriptor> schema = connection_.schema();

  if (pending.kind == Pending::Kind::Step) {
    if (!pending.step->has_value()) {
      Reply reply;
      reply.command = pending.command;
      reply.outcome = Outcome::Submitted;
      return reply;
    }
    const auto result = std::move(**pending.step);
    const std::string command = pending.command;
    const bool capture = pending.capture;
    pending_.reset();
    if (result.isErr()) return failed(command, controlError(result.error()));
    return done(command, stepText(result.value(), capture), stepJson(result.value(), capture));
  }

  // A transaction is empty when it has no commands; it then has no outcome.
  const bool isTransaction = pending.kind == Pending::Kind::Transaction;
  const auto tickets = pending.tickets;
  const auto ticketValue = [&] {
    if (isTransaction) {
      Value ticket = Value::object();
      ticket.set("first", Value::number(tickets.first.value));
      ticket.set("count", Value::number(tickets.count));
      return ticket;
    }
    return Value::number(tickets.first.value);
  };
  for (const access::CommandFailure& failure : connection_.drainFailures()) {
    if (!tickets.contains(failure.ticket)) continue;
    const std::string command = pending.command;
    const std::uint64_t at = failure.ticket.value - tickets.first.value;
    Value facts = Value::object();
    facts.set("ticket", Value::number(failure.ticket.value));
    if (tickets.count > 1) facts.set("at", Value::number(at));
    CliError error = refusedError(failure.error, std::move(facts));
    Value result = Value::object();
    if (isTransaction) {
      result.set("ticket", ticketValue());
      result.set("committed", Value::boolean(false));
      result.set("at", Value::number(at));
      result.set("error", Value::string(std::string(access::toString(failure.error))));
      error.text += " at " + std::to_string(at);
    } else {
      Value refused = Value::object();
      refused.set("code", Value::string(std::string(access::toString(failure.error))));
      if (tickets.count > 1) refused.set("at", Value::number(at));
      Value outcome = Value::object();
      outcome.set("refused", std::move(refused));
      result.set("ticket", Value::number(tickets.first.value));
      if (tickets.count > 1) result.set("count", Value::number(tickets.count));
      result.set("outcome", std::move(outcome));
    }
    abandon();
    Reply reply = failed(command, std::move(error));
    reply.result = std::move(result);
    return reply;
  }
  const auto events = connection_.drainEvents(pending.subscription);
  if (events.isOk()) {
    for (const access::Event& event : events.value()) pending.seen.push_back(event);
  }
  // The write's events: one per command, in order, contiguous (a
  // transaction applies whole, Spec 0053).
  const std::size_t n = pending.commands.size();
  for (std::size_t start = 0; n > 0 && start + n <= pending.seen.size(); ++start) {
    bool match = true;
    for (std::size_t i = 0; i < n && match; ++i) match = produces(pending.commands[i], pending.seen[start + i]);
    if (!match) continue;
    std::string human;
    Value eventValues = Value::array();
    for (std::size_t i = 0; i < n; ++i) {
      human += "ok " + eventText(schema, pending.seen[start + i]) + "\n";
      eventValues.push(eventJson(schema, pending.seen[start + i]));
    }
    Value result = Value::object();
    if (isTransaction) {
      result.set("ticket", ticketValue());
      result.set("committed", Value::boolean(true));
      result.set("events", std::move(eventValues));
      human += "committed " + std::to_string(n) + "\n";
    } else {
      Value outcome = Value::object();
      outcome.set("events", std::move(eventValues));
      result.set("ticket", Value::number(tickets.first.value));
      if (tickets.count > 1) result.set("count", Value::number(tickets.count));
      result.set("outcome", std::move(outcome));
    }
    const std::string command = pending.command;
    abandon();
    return done(command, std::move(human), std::move(result));
  }
  if (n == 0) {  // an empty transaction: nothing to apply
    Value result = Value::object();
    result.set("ticket", ticketValue());
    result.set("committed", Value::boolean(true));
    result.set("events", Value::array());
    abandon();
    return done("tx", "committed 0\n", std::move(result));
  }
  Reply reply;
  reply.command = pending.command;
  reply.outcome = Outcome::Submitted;
  return reply;
}

}  // namespace atlantis::cli
