#include <atlantis/editor/model/runtime_world_edit_target.h>

#include <algorithm>
#include <variant>

namespace atlantis::editor {

namespace access = atlantis::world::access;
namespace connection = atlantis::connection;

namespace {

// Spec 0049 R7's address is the access layer's, field for field (ADR-0124 D4).
[[nodiscard]] access::PropertyAddress toAccess(const atlantis::asset_system::scene::PropertyAddress& address) {
  return access::PropertyAddress{address.node, address.component, address.field};
}

[[nodiscard]] bool sameAddress(const atlantis::asset_system::scene::PropertyAddress& a,
                               const access::PropertyAddress& b) {
  return a.node == b.entity && a.component == b.component && a.field == b.field;
}

}  // namespace

RuntimeWorldEditTarget::RuntimeWorldEditTarget(connection::RuntimeConnection& connection) : connection_(connection) {
  connection::EventFilter filter;
  filter.kinds = connection::EventKindSet::only(connection::EventKind::PropertyChanged)
                     .with(connection::EventKind::EntityDestroyed)
                     .with(connection::EventKind::ComponentRemoved);
  subscription_ = connection_.subscribe(filter);
}

RuntimeWorldEditTarget::~RuntimeWorldEditTarget() { (void)connection_.unsubscribe(subscription_); }

std::optional<access::PropertyValue> RuntimeWorldEditTarget::read(
    const atlantis::asset_system::scene::PropertyAddress& address) {
  auto value = connection_.getProperty(toAccess(address));
  if (value.isErr()) return std::nullopt;
  return value.value();
}

EditHandle RuntimeWorldEditTarget::submit(std::span<const AddressedValue> writes) {
  std::vector<access::Command> commands;
  commands.reserve(writes.size());
  for (const auto& [address, value] : writes) commands.emplace_back(access::SetProperty{toAccess(address), value});
  const EditHandle handle = handleOf(connection_.submitTransaction(std::move(commands)));
  track(handle, writes);
  return handle;
}

void RuntimeWorldEditTarget::track(EditHandle handle, std::span<const AddressedValue> writes) {
  tracked_.push_back(Tracked{handle, std::vector<AddressedValue>(writes.begin(), writes.end()), 0,
                             Settlement::Pending, {}});
}

Settlement RuntimeWorldEditTarget::settlement(EditHandle handle) const {
  for (const Tracked& tracked : tracked_) {
    if (tracked.handle == handle) return tracked.settlement;
  }
  return Settlement::Pending;
}

std::string RuntimeWorldEditTarget::refusalText(EditHandle handle) const {
  for (const Tracked& tracked : tracked_) {
    if (tracked.handle == handle) return tracked.refusal;
  }
  return {};
}

void RuntimeWorldEditTarget::update(std::span<const access::CommandFailure> failures) {
  // Handles settled by the last update() have been read by now.
  tracked_.erase(std::remove_if(tracked_.begin(), tracked_.end(),
                                [](const Tracked& tracked) { return tracked.settlement != Settlement::Pending; }),
                 tracked_.end());
  for (Tracked& tracked : tracked_) {
    for (const access::CommandFailure& failure : failures) {
      const std::uint64_t ticket = failure.ticket.value;
      if (tracked.handle.count != 0 && tracked.handle.first <= ticket && ticket - tracked.handle.first < tracked.handle.count) {
        tracked.settlement = Settlement::Refused;
        tracked.refusal = std::string(access::toString(failure.error));
      }
    }
  }
  auto events = connection_.drainEvents(subscription_);
  if (events.isErr()) return;  // only an unknown subscription, which ours never is
  for (const access::Event& event : events.value()) {
    if (const auto* changed = std::get_if<access::PropertyChanged>(&event)) {
      for (Tracked& tracked : tracked_) {
        if (tracked.settlement != Settlement::Pending || tracked.seen >= tracked.writes.size()) continue;
        const AddressedValue& next = tracked.writes[tracked.seen];
        if (sameAddress(next.first, changed->address) && next.second == changed->value) {
          if (++tracked.seen == tracked.writes.size()) tracked.settlement = Settlement::Applied;
        }
      }
    } else if (const auto* destroyed = std::get_if<access::EntityDestroyed>(&event)) {
      invalidations_.push_back(Invalidation{destroyed->entity, std::nullopt});
    } else if (const auto* removed = std::get_if<access::ComponentRemoved>(&event)) {
      invalidations_.push_back(Invalidation{removed->entity, removed->component});
    }
  }
}

std::vector<Invalidation> RuntimeWorldEditTarget::drainInvalidations() {
  std::vector<Invalidation> out;
  out.swap(invalidations_);
  return out;
}

}  // namespace atlantis::editor
