#pragma once

#include <atlantis/connection/runtime_connection.h>
#include <atlantis/editor/model/edit_target.h>

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Spec 0063 R3, R6, R8 / ADR-0124 D3, D4 (Plan 0063 P3): E2's one edit target,
// the Runtime World through a RuntimeConnection. A Spec 0049 address becomes
// the World access layer's field for field. One event subscription
// (PropertyChanged, EntityDestroyed, ComponentRemoved) is drained once per
// update(): a tracked transaction is applied when its writes' PropertyChanged
// events have all been seen, in order, with the written values (Plan 0063
// J7); refused when a drained failure's ticket falls inside it. UI-free.
namespace atlantis::editor {

// Borrows the connection, which must outlive it. Frame thread only, between
// Runtime frames (ADR-0105); not thread-safe.
class RuntimeWorldEditTarget final : public EditTarget {
 public:
  explicit RuntimeWorldEditTarget(atlantis::connection::RuntimeConnection& connection);
  ~RuntimeWorldEditTarget() override;
  RuntimeWorldEditTarget(const RuntimeWorldEditTarget&) = delete;
  RuntimeWorldEditTarget& operator=(const RuntimeWorldEditTarget&) = delete;

  [[nodiscard]] std::optional<atlantis::world::access::PropertyValue> read(
      const atlantis::asset_system::scene::PropertyAddress& address) override;
  EditHandle submit(std::span<const AddressedValue> writes) override;
  void track(EditHandle handle, std::span<const AddressedValue> writes) override;
  [[nodiscard]] Settlement settlement(EditHandle handle) const override;
  [[nodiscard]] std::string refusalText(EditHandle handle) const override;
  void update(std::span<const atlantis::world::access::CommandFailure> failures) override;
  [[nodiscard]] std::vector<Invalidation> drainInvalidations() override;

  // A one-command edit's handle: a transaction of one (Plan 0063 J2).
  [[nodiscard]] static EditHandle handleOf(atlantis::world::access::CommandTicket ticket) noexcept {
    return EditHandle{ticket.value, 1};
  }
  [[nodiscard]] static EditHandle handleOf(atlantis::world::access::TransactionTicket ticket) noexcept {
    return EditHandle{ticket.first.value, ticket.count};
  }

 private:
  struct Tracked {
    EditHandle handle;
    std::vector<AddressedValue> writes;
    std::size_t seen = 0;  // writes whose PropertyChanged has arrived, in order
    Settlement settlement = Settlement::Pending;
    std::string refusal;
  };

  atlantis::connection::RuntimeConnection& connection_;
  atlantis::connection::SubscriptionId subscription_;
  std::vector<Tracked> tracked_;
  std::vector<Invalidation> invalidations_;
};

}  // namespace atlantis::editor
