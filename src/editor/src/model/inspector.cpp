#include <atlantis/editor/model/inspector.h>

#include <atlantis/assert.h>
#include <atlantis/connection/text.h>

#include <variant>

namespace atlantis::editor {

namespace access = atlantis::world::access;
namespace connection = atlantis::connection;
using atlantis::asset_system::EntityGuid;

namespace {

[[nodiscard]] FieldControl controlFor(const schema::FieldDescriptor& leaf) {
  if (leaf.kind == schema::TypeKind::Enum) return FieldControl::Enum;
  switch (leaf.primitive) {
    case schema::PrimitiveKind::UInt64: return FieldControl::Integer;
    case schema::PrimitiveKind::Float32: return FieldControl::Float;
    case schema::PrimitiveKind::Vec3Float32: return FieldControl::Float3;
    case schema::PrimitiveKind::Vec4Float32: return FieldControl::Float4;
    case schema::PrimitiveKind::AssetGuid:
    case schema::PrimitiveKind::EntityGuid: return FieldControl::Guid;
  }
  return FieldControl::Integer;
}

// "Camera.fog.density" -> {"Camera", "fog", "density"}.
[[nodiscard]] std::vector<std::string> segmentsOf(const std::string& path) {
  std::vector<std::string> segments;
  std::size_t start = 0;
  while (true) {
    const std::size_t dot = path.find('.', start);
    segments.push_back(path.substr(start, dot == std::string::npos ? std::string::npos : dot - start));
    if (dot == std::string::npos) break;
    start = dot + 1;
  }
  return segments;
}

}  // namespace

InspectorModel::InspectorModel(connection::RuntimeConnection& connection) : connection_(connection) {}

InspectorModel::~InspectorModel() {
  if (subscription_.has_value()) (void)connection_.unsubscribe(*subscription_);
}

void InspectorModel::setSubject(std::optional<EntityGuid> entity) {
  if (entity == subject_) return;
  if (subscription_.has_value()) (void)connection_.unsubscribe(*subscription_);
  subscription_.reset();
  subject_ = entity;
  components_.clear();
  fields_.clear();
  outcomes_.clear();
  if (!subject_.has_value()) return;
  connection::EventFilter filter;
  filter.kinds = connection::EventKindSet::only(connection::EventKind::PropertyChanged);
  filter.entity = subject_;
  subscription_ = connection_.subscribe(filter);
  auto components = connection_.listComponents(*subject_);
  if (components.isOk()) components_ = std::move(components.value());
  rebuild();
  readValues();
}

void InspectorModel::update(std::span<const access::CommandFailure> failures) {
  if (!subject_.has_value()) return;
  for (std::optional<EditOutcome>& outcome : outcomes_) {
    if (!outcome.has_value() || outcome->status != EditStatus::Pending) continue;
    for (const access::CommandFailure& failure : failures) {
      if (failure.ticket == outcome->ticket) {
        outcome->status = EditStatus::Refused;
        outcome->detail = std::string(access::toString(failure.error));
      }
    }
  }
  if (subscription_.has_value()) {
    auto events = connection_.drainEvents(*subscription_);
    if (events.isOk()) {
      for (const access::Event& event : events.value()) {
        const auto* changed = std::get_if<access::PropertyChanged>(&event);
        if (changed == nullptr) continue;
        for (std::size_t i = 0; i < fields_.size(); ++i) {
          std::optional<EditOutcome>& outcome = outcomes_[i];
          if (outcome.has_value() && outcome->status == EditStatus::Pending &&
              fields_[i].component == changed->address.component && fields_[i].field == changed->address.field) {
            outcome->status = EditStatus::Applied;
          }
        }
      }
    }
  }
  auto components = connection_.listComponents(*subject_);
  std::vector<schema::TypeId> current = components.isOk() ? std::move(components.value()) : std::vector<schema::TypeId>{};
  if (current != components_) {
    components_ = std::move(current);
    rebuild();
  }
  readValues();
}

// Generated from the schema alone (R3): each component's leaves in descriptor
// order, nested structs as groups, enum constants as choices, the Editable and
// Optional flags as they are declared.
void InspectorModel::rebuild() {
  fields_.clear();
  outcomes_.clear();
  const std::span<const schema::TypeDescriptor> schema = connection_.schema();
  for (const schema::TypeId component : components_) {
    for (const connection::text::LeafPath& leafPath : connection::text::leavesOf(schema, component)) {
      const schema::FieldDescriptor& leaf = *leafPath.leaf;
      InspectorField field;
      field.component = component;
      field.path = leafPath.path;
      std::vector<std::string> segments = segmentsOf(leafPath.path);
      field.label = segments.back();
      field.groups.assign(segments.begin() + 1, segments.end() - 1);
      field.field = leaf.id;
      field.kind = leaf.kind;
      field.primitive = leaf.primitive;
      field.control = controlFor(leaf);
      field.editable = schema::hasFlags(leaf.flags, schema::FieldFlags::Editable);
      field.optional = schema::hasFlags(leaf.flags, schema::FieldFlags::Optional);
      if (leaf.kind == schema::TypeKind::Enum) {
        if (const schema::TypeDescriptor* enumType = connection::text::findType(schema, leaf.type)) {
          for (const schema::EnumConstantDescriptor& constant : enumType->constants) {
            field.choices.push_back(EnumChoice{std::string(constant.name), constant.value});
          }
        }
      }
      fields_.push_back(std::move(field));
    }
  }
  outcomes_.resize(fields_.size());
}

void InspectorModel::readValues() {
  for (InspectorField& field : fields_) {
    auto value = connection_.getProperty(access::PropertyAddress{*subject_, field.component, field.field});
    field.value = value.isOk() ? std::optional<access::PropertyValue>(std::move(value.value())) : std::nullopt;
  }
}

std::string_view InspectorModel::componentName(schema::TypeId component) const {
  const schema::TypeDescriptor* type = connection::text::findType(connection_.schema(), component);
  return type != nullptr ? connection::text::shortName(type->name) : std::string_view("?");
}

std::string_view InspectorModel::componentNote(schema::TypeId component) const {
  return componentName(component) == "Transform" ? kTransformNote : std::string_view();
}

access::CommandTicket InspectorModel::commit(std::size_t index, access::PropertyValue value) {
  ATLANTIS_CHECK_MSG(subject_.has_value() && index < fields_.size() && fields_[index].editable,
                     "InspectorModel::commit(): no such editable field");
  const InspectorField& field = fields_[index];
  const access::CommandTicket ticket = connection_.submit(
      access::SetProperty{access::PropertyAddress{*subject_, field.component, field.field}, std::move(value)});
  outcomes_[index] = EditOutcome{EditStatus::Pending, ticket, {}};
  return ticket;
}

const std::optional<EditOutcome>& InspectorModel::outcome(std::size_t index) const {
  ATLANTIS_CHECK(index < outcomes_.size());
  return outcomes_[index];
}

}  // namespace atlantis::editor
