#include <atlantis/gameplay/world.h>

#include <atlantis/connection/text.h>

#include <algorithm>
#include <set>
#include <string>

// Spec 0057 R5, ADR-0111 D4, Plan 0057 P4: a generated binding against the
// connection's schema -- the type's id, kind and version, its exact field
// set (name, id, kind, primitive, referenced TypeId, Optional, Editable),
// its constants by name and value, and recursively every struct or enum its
// fields reference. Cached per TypeId for the World's lifetime.
namespace atlantis::gameplay {

namespace {

namespace text = ::atlantis::connection::text;

[[nodiscard]] const TypeBinding* bindingWithId(std::span<const TypeBinding> bindings, schema::TypeId id) {
  for (const TypeBinding& binding : bindings) {
    if (binding.id == id) return &binding;
  }
  return nullptr;
}

[[nodiscard]] bool sameField(const FieldBinding& b, const schema::FieldDescriptor& d) {
  if (b.name != d.name || b.id != d.id || b.kind != d.kind || b.type != d.type) return false;
  if (b.kind == schema::TypeKind::Primitive && b.primitive != d.primitive) return false;
  return b.optional == schema::hasFlags(d.flags, schema::FieldFlags::Optional) &&
         b.editable == schema::hasFlags(d.flags, schema::FieldFlags::Editable);
}

// Whether `binding` and every type it references match `schema`; `seen`
// stops a type being checked twice.
[[nodiscard]] bool matches(std::span<const TypeBinding> bindings, const TypeBinding& binding,
                           std::span<const schema::TypeDescriptor> schemaSpan, std::set<std::uint64_t>& seen) {
  if (!seen.insert(binding.id.value).second) return true;
  const schema::TypeDescriptor* descriptor = text::findType(schemaSpan, binding.id);
  if (descriptor == nullptr) return false;
  if (descriptor->name != binding.name || descriptor->kind != binding.kind ||
      descriptor->schemaVersion != binding.schemaVersion) {
    return false;
  }
  if (descriptor->fields.size() != binding.fields.size()) return false;
  for (const FieldBinding& field : binding.fields) {
    const auto found = std::find_if(descriptor->fields.begin(), descriptor->fields.end(),
                                    [&](const schema::FieldDescriptor& d) { return d.id == field.id; });
    if (found == descriptor->fields.end() || !sameField(field, *found)) return false;
  }
  if (descriptor->constants.size() != binding.constants.size()) return false;
  for (const EnumConstantBinding& constant : binding.constants) {
    const auto found = std::find_if(descriptor->constants.begin(), descriptor->constants.end(),
                                    [&](const schema::EnumConstantDescriptor& d) { return d.name == constant.name; });
    if (found == descriptor->constants.end() || found->value != constant.value) return false;
  }
  for (const FieldBinding& field : binding.fields) {
    if (field.kind == schema::TypeKind::Primitive) continue;
    const TypeBinding* referenced = bindingWithId(bindings, field.type);
    if (referenced == nullptr || !matches(bindings, *referenced, schemaSpan, seen)) return false;
  }
  return true;
}

}  // namespace

atlantis::Result<std::monostate, Error> World::checkBinding(std::span<const TypeBinding> bindings,
                                                            std::size_t index) const {
  using ResultT = atlantis::Result<std::monostate, Error>;
  const TypeBinding& binding = bindings[index];
  auto cached = compatible_.find(binding.id.value);
  if (cached == compatible_.end()) {
    std::set<std::uint64_t> seen;
    cached = compatible_.emplace(binding.id.value, matches(bindings, binding, connection_.schema(), seen)).first;
  }
  if (!cached->second) return ResultT::Err(Error::mismatch(std::string(binding.name)));
  return ResultT::Ok(std::monostate{});
}

}  // namespace atlantis::gameplay
