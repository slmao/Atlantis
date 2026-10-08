#include <atlantis/editor/model/hierarchy.h>

#include <atlantis/connection/text.h>

#include <algorithm>
#include <cctype>
#include <variant>

namespace atlantis::editor {

namespace access = atlantis::world::access;
namespace connection = atlantis::connection;
using atlantis::asset_system::EntityGuid;

namespace {

[[nodiscard]] bool containsIgnoringCase(std::string_view haystack, std::string_view needle) {
  if (needle.size() > haystack.size()) return false;
  for (std::size_t at = 0; at + needle.size() <= haystack.size(); ++at) {
    bool match = true;
    for (std::size_t i = 0; i < needle.size() && match; ++i) {
      match = std::tolower(static_cast<unsigned char>(haystack[at + i])) ==
              std::tolower(static_cast<unsigned char>(needle[i]));
    }
    if (match) return true;
  }
  return false;
}

[[nodiscard]] const EntityGuid& entityOf(const access::Event& event) {
  return std::visit(
      [](const auto& e) -> const EntityGuid& {
        if constexpr (requires { e.entity; }) {
          return e.entity;
        } else {
          return e.address.entity;
        }
      },
      event);
}

}  // namespace

HierarchyModel::HierarchyModel(connection::RuntimeConnection& connection) : connection_(connection) {}

HierarchyModel::~HierarchyModel() {
  if (subscription_.has_value()) (void)connection_.unsubscribe(*subscription_);
}

void HierarchyModel::update() {
  if (!subscription_.has_value()) {
    connection::EventFilter filter;
    filter.kinds = connection::EventKindSet::only(connection::EventKind::EntityCreated)
                       .with(connection::EventKind::EntityDestroyed)
                       .with(connection::EventKind::ComponentAdded)
                       .with(connection::EventKind::ComponentRemoved);
    subscription_ = connection_.subscribe(filter);
    listAll();
    return;
  }
  auto events = connection_.drainEvents(*subscription_);
  if (events.isErr()) return;  // only an unknown subscription, which ours never is
  std::vector<EntityGuid> touched;
  for (const access::Event& event : events.value()) touched.push_back(entityOf(event));
  std::sort(touched.begin(), touched.end());
  touched.erase(std::unique(touched.begin(), touched.end()), touched.end());
  for (const EntityGuid& entity : touched) refresh(entity);
}

void HierarchyModel::listAll() {
  rows_.clear();
  for (const EntityGuid& entity : connection_.listEntities()) {
    auto components = connection_.listComponents(entity);
    if (components.isOk()) rows_.push_back(HierarchyRow{entity, std::move(components.value())});
  }
  if (selection_.has_value()) select(selection_);
}

// Brings one entity's row in line with the World now: whatever sequence of
// events named it this frame, the result is what a fresh listing would show.
void HierarchyModel::refresh(const EntityGuid& entity) {
  const auto at = std::lower_bound(rows_.begin(), rows_.end(), entity,
                                   [](const HierarchyRow& row, const EntityGuid& guid) { return row.entity < guid; });
  const bool listed = at != rows_.end() && at->entity == entity;
  auto components = connection_.listComponents(entity);
  if (components.isErr()) {
    if (listed) rows_.erase(at);
    if (selection_ == entity) selection_.reset();
    return;
  }
  if (listed) {
    at->components = std::move(components.value());
  } else {
    rows_.insert(at, HierarchyRow{entity, std::move(components.value())});
  }
}

std::vector<std::size_t> HierarchyModel::filtered(std::string_view filter) const {
  std::vector<std::size_t> out;
  for (std::size_t i = 0; i < rows_.size(); ++i) {
    bool keep = filter.empty();
    if (!keep) {
      const std::string guid = atlantis::asset_system::toString(rows_[i].entity);
      keep = guid.size() >= filter.size() && containsIgnoringCase(std::string_view(guid).substr(0, filter.size()), filter);
    }
    for (std::size_t c = 0; !keep && c < rows_[i].components.size(); ++c) {
      keep = containsIgnoringCase(componentName(rows_[i].components[c]), filter);
    }
    if (keep) out.push_back(i);
  }
  return out;
}

std::string_view HierarchyModel::componentName(schema::TypeId component) const {
  const schema::TypeDescriptor* type = connection::text::findType(connection_.schema(), component);
  return type != nullptr ? connection::text::shortName(type->name) : std::string_view("?");
}

std::string HierarchyModel::componentNames(const HierarchyRow& row) const {
  std::string names;
  for (const schema::TypeId component : row.components) {
    if (!names.empty()) names += ", ";
    names += componentName(component);
  }
  return names;
}

void HierarchyModel::select(std::optional<EntityGuid> entity) {
  if (entity.has_value()) {
    const bool listed = std::binary_search(
        rows_.begin(), rows_.end(), HierarchyRow{*entity, {}},
        [](const HierarchyRow& a, const HierarchyRow& b) { return a.entity < b.entity; });
    if (!listed) entity.reset();
  }
  selection_ = entity;
}

}  // namespace atlantis::editor
