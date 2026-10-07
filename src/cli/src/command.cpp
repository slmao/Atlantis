#include <atlantis/cli/command.h>

#include <atlantis/connection/text.h>

#include <cstddef>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace atlantis::cli {

namespace access = atlantis::world::access;
namespace text = atlantis::connection::text;
using atlantis::asset_system::EntityGuid;
using atlantis::connection::EventFilter;
using atlantis::connection::EventKind;
using atlantis::connection::EventKindSet;
using schema::FieldDescriptor;
using schema::FieldFlags;
using schema::PrimitiveKind;
using schema::TypeDescriptor;
using schema::TypeId;
using schema::TypeKind;

namespace {

[[nodiscard]] std::vector<std::string_view> tokenize(std::string_view line) {
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

[[nodiscard]] std::string flagNames(FieldFlags flags) {
  static constexpr std::pair<FieldFlags, std::string_view> kNames[] = {
      {FieldFlags::Serializable, "serializable"}, {FieldFlags::Editable, "editable"},
      {FieldFlags::AssetReference, "assetref"},   {FieldFlags::EntityReference, "entityref"},
      {FieldFlags::Optional, "optional"},
  };
  std::string out;
  for (const auto& [flag, name] : kNames) {
    if (!schema::hasFlags(flags, flag)) continue;
    if (!out.empty()) out += ',';
    out += name;
  }
  return out.empty() ? "-" : out;
}

}  // namespace

Commands::Commands(atlantis::connection::RuntimeConnection& connection, std::ostream& out)
    : connection_(connection), out_(out) {}

Commands::~Commands() {
  if (pending_) (void)connection_.unsubscribe(pending_->subscription);
}

Outcome Commands::run(std::string_view line) {
  const std::vector<std::string_view> t = tokenize(line);
  const std::span<const TypeDescriptor> schema = connection_.schema();
  const auto error = [&](std::string_view message, std::string_view detail = {}) {
    out_ << "error: " << message;
    if (!detail.empty()) out_ << " '" << detail << "'";
    out_ << '\n';
    return Outcome::Error;
  };
  const auto refused = [&](access::AccessError why) {
    out_ << "refused " << access::toString(why) << '\n';
    return Outcome::Refused;
  };
  const auto textError = [&](text::TextError why, std::string_view token) { return error(text::toString(why), token); };
  // The canonical path of a resolved leaf (short type name).
  const auto canonicalPath = [&](const text::ResolvedPath& resolved) {
    for (const text::LeafPath& leaf : text::leavesOf(schema, resolved.component)) {
      if (leaf.leaf == resolved.leaf) return leaf.path;
    }
    return std::string();
  };

  if (t.empty()) return error("empty command");
  if (pending_) return error("the previous `entity set` has no outcome yet");

  if (t.size() == 2 && t[0] == "schema" && t[1] == "list") {
    for (const TypeDescriptor& type : schema) out_ << text::shortName(type.name) << ' ' << kindName(type.kind) << '\n';
    return Outcome::Done;
  }
  if (t[0] == "schema" && t.size() >= 2 && t[1] == "inspect") {
    if (t.size() != 3) return error("usage: schema inspect <Type>");
    const TypeDescriptor* type = text::findType(schema, t[2]);
    if (type == nullptr) return textError(text::TextError::UnknownType, t[2]);
    out_ << text::shortName(type->name) << ' ' << kindName(type->kind) << " (" << type->name << ") v"
         << type->schemaVersion << '\n';
    for (const FieldDescriptor& field : type->fields) {
      out_ << "  " << field.name << ' ';
      if (field.kind == TypeKind::Primitive) {
        out_ << primitiveName(field.primitive);
      } else {
        const TypeDescriptor* referenced = text::findType(schema, field.type);
        out_ << kindName(field.kind) << ' ' << (referenced ? text::shortName(referenced->name) : "?");
      }
      out_ << ' ' << flagNames(field.flags) << '\n';
    }
    for (const auto& constant : type->constants) out_ << "  " << constant.name << " = " << constant.value << '\n';
    return Outcome::Done;
  }
  if (t.size() == 2 && t[0] == "world" && t[1] == "entities") {
    for (const EntityGuid& entity : connection_.listEntities()) {
      out_ << text::formatEntity(entity) << ' ';
      const auto components = connection_.listComponents(entity);
      std::string names;
      if (components.isOk()) {
        for (const TypeId component : components.value()) {
          const TypeDescriptor* type = text::findType(schema, component);
          if (!names.empty()) names += ',';
          names += type ? std::string(text::shortName(type->name)) : "?";
        }
      }
      out_ << (names.empty() ? "-" : names) << '\n';
    }
    return Outcome::Done;
  }
  if (t[0] == "entity" && t.size() >= 2 && t[1] == "inspect") {
    if (t.size() != 3) return error("usage: entity inspect <guid>");
    const auto entity = text::parseEntity(t[2]);
    if (entity.isErr()) return textError(entity.error(), t[2]);
    const auto components = connection_.listComponents(entity.value());
    if (components.isErr()) return refused(components.error());
    out_ << text::formatEntity(entity.value()) << '\n';
    for (const TypeId component : components.value()) {
      for (const text::LeafPath& leaf : text::leavesOf(schema, component)) {
        const auto value = connection_.getProperty({entity.value(), component, leaf.leaf->id});
        out_ << "  " << leaf.path << " = ";
        if (value.isOk()) {
          out_ << text::formatValue(schema, *leaf.leaf, value.value());
        } else {
          out_ << "(refused " << access::toString(value.error()) << ')';
        }
        out_ << '\n';
      }
    }
    return Outcome::Done;
  }
  if (t[0] == "entity" && t.size() >= 2 && t[1] == "get") {
    if (t.size() != 4) return error("usage: entity get <guid> <Type>.<field>");
    const auto entity = text::parseEntity(t[2]);
    if (entity.isErr()) return textError(entity.error(), t[2]);
    const auto path = text::parsePath(schema, t[3]);
    if (path.isErr()) return textError(path.error(), t[3]);
    const auto value = connection_.getProperty({entity.value(), path.value().component, path.value().leaf->id});
    if (value.isErr()) return refused(value.error());
    out_ << text::formatValue(schema, *path.value().leaf, value.value()) << '\n';
    return Outcome::Done;
  }
  if (t[0] == "entity" && t.size() >= 2 && t[1] == "set") {
    if (t.size() < 5) return error("usage: entity set <guid> <Type>.<field> <value...>");
    const auto entity = text::parseEntity(t[2]);
    if (entity.isErr()) return textError(entity.error(), t[2]);
    const auto path = text::parsePath(schema, t[3]);
    if (path.isErr()) return textError(path.error(), t[3]);
    const std::span<const std::string_view> valueTokens(t.data() + 4, t.size() - 4);
    const auto value = text::parseValue(schema, *path.value().leaf, valueTokens);
    if (value.isErr()) return textError(value.error(), t[4]);
    const access::PropertyAddress address{entity.value(), path.value().component, path.value().leaf->id};
    const auto subscription = connection_.subscribe(
        EventFilter{EventKindSet::only(EventKind::PropertyChanged), address.entity, address.component});
    const access::CommandTicket ticket = connection_.submit(access::SetProperty{address, value.value()});
    pending_ = Pending{ticket, subscription, address, canonicalPath(path.value()), path.value().leaf};
    return Outcome::Submitted;
  }
  return error("unknown command", line);
}

Outcome Commands::reportPending() {
  if (!pending_) return Outcome::Done;
  const Pending& pending = *pending_;
  std::optional<Outcome> outcome;
  for (const access::CommandFailure& failure : connection_.drainFailures()) {
    if (failure.ticket == pending.ticket) {
      out_ << "refused " << access::toString(failure.error) << '\n';
      outcome = Outcome::Refused;
    }
  }
  const auto events = connection_.drainEvents(pending.subscription);
  if (!outcome && events.isOk()) {
    for (const access::Event& event : events.value()) {
      const auto* changed = std::get_if<access::PropertyChanged>(&event);
      if (changed == nullptr || !(changed->address == pending.address)) continue;
      out_ << "ok " << text::formatEntity(pending.address.entity) << ' ' << pending.path << " = "
           << text::formatValue(connection_.schema(), *pending.leaf, changed->value) << '\n';
      outcome = Outcome::Done;
      break;
    }
  }
  if (!outcome) return Outcome::Submitted;  // not applied yet: still pending
  (void)connection_.unsubscribe(pending.subscription);
  pending_.reset();
  return *outcome;
}

}  // namespace atlantis::cli
