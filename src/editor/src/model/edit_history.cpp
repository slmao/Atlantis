#include <atlantis/editor/model/edit_history.h>

#include <algorithm>
#include <optional>
#include <utility>

namespace atlantis::editor {

namespace {

// The writes of one direction: `after` forward, `before` inverse.
[[nodiscard]] std::vector<AddressedValue> writesOf(const EditTransaction& edit, bool forward) {
  std::vector<AddressedValue> writes;
  writes.reserve(edit.writes.size());
  for (const PropertyWrite& write : edit.writes) writes.emplace_back(write.address, forward ? write.after : write.before);
  return writes;
}

[[nodiscard]] bool names(const EditTransaction& edit, const Invalidation& invalidation) {
  return std::any_of(edit.writes.begin(), edit.writes.end(), [&](const PropertyWrite& write) {
    return write.address.node == invalidation.entity &&
           (!invalidation.component.has_value() || write.address.component == *invalidation.component);
  });
}

[[nodiscard]] std::string goneText(const Invalidation& invalidation) {
  return invalidation.component.has_value() ? "a component it edited was removed" : "an entity it edited was destroyed";
}

}  // namespace

EditHistory::EditHistory(EditTarget& target) : target_(target) {}

void EditHistory::record(EditTransaction edit, EditHandle handle) {
  if (edit.writes.empty()) return;
  redo_.clear();
  for (Pending& pending : pending_) pending.redoCleared = true;
  const std::vector<AddressedValue> writes = writesOf(edit, true);
  target_.track(handle, writes);
  pending_.push_back(Pending{std::move(edit), handle, Step::Record, false});
}

HistoryResult EditHistory::undo() { return step(undo_, Step::Undo); }

HistoryResult EditHistory::redo() { return step(redo_, Step::Redo); }

// Undo reads `after` where the edit left it and writes `before`; redo the
// reverse. On a mismatch or an unreadable address nothing is submitted and
// the entry is dropped (ruling Q4, K-a).
HistoryResult EditHistory::step(std::deque<EditTransaction>& from, Step kind) {
  if (waiting()) {
    notice_ = "waiting for the Runtime";
    return HistoryResult::Waiting;
  }
  if (from.empty()) return HistoryResult::NothingTo;
  const bool forward = kind == Step::Redo;
  const EditTransaction& edit = from.back();
  for (const PropertyWrite& write : edit.writes) {
    const std::optional<atlantis::world::access::PropertyValue> current = target_.read(write.address);
    if (!current.has_value()) {
      notice_ = "\"" + edit.label + "\" dropped: what it edited is gone";
      from.pop_back();
      return HistoryResult::Invalidated;
    }
    if (*current != (forward ? write.before : write.after)) {
      notice_ = "\"" + edit.label + "\" dropped: changed elsewhere";
      from.pop_back();
      return HistoryResult::ChangedElsewhere;
    }
  }
  EditTransaction taken = std::move(from.back());
  from.pop_back();
  const std::vector<AddressedValue> writes = writesOf(taken, forward);
  const EditHandle handle = target_.submit(writes);
  notice_ = std::string(forward ? "redo " : "undo ") + taken.label;
  pending_.push_back(Pending{std::move(taken), handle, kind, false});
  return HistoryResult::Submitted;
}

void EditHistory::push(std::deque<EditTransaction>& stack, EditTransaction edit) {
  stack.push_back(std::move(edit));
  while (stack.size() > kMaxDepth) stack.pop_front();
}

void EditHistory::update(std::span<const atlantis::world::access::CommandFailure> failures) {
  target_.update(failures);
  const std::vector<Invalidation> invalidations = target_.drainInvalidations();
  // Settle in submission order: an applied edit joins its stack before any
  // invalidation of this frame is applied to the stacks.
  std::vector<Pending> still;
  for (Pending& pending : pending_) {
    for (const Invalidation& invalidation : invalidations) {
      if (names(pending.edit, invalidation)) pending.invalidated = true;
    }
    switch (target_.settlement(pending.handle)) {
      case Settlement::Pending: still.push_back(std::move(pending)); break;
      case Settlement::Refused:
        notice_ = "\"" + pending.edit.label + "\" refused: " + target_.refusalText(pending.handle);
        break;
      case Settlement::Applied:
        if (pending.invalidated) {
          notice_ = "\"" + pending.edit.label + "\" dropped: what it edited is gone";
          break;
        }
        // An applied edit or redo is undoable; an applied undo is redoable,
        // unless a newer edit has cleared redo since.
        if (pending.step != Step::Undo) {
          push(undo_, std::move(pending.edit));
        } else if (!pending.redoCleared) {
          push(redo_, std::move(pending.edit));
        }
        break;
    }
  }
  pending_ = std::move(still);
  for (const Invalidation& invalidation : invalidations) invalidate(invalidation);
}

void EditHistory::invalidate(const Invalidation& invalidation) {
  for (std::deque<EditTransaction>* stack : {&undo_, &redo_}) {
    const auto before = stack->size();
    stack->erase(std::remove_if(stack->begin(), stack->end(),
                                [&](const EditTransaction& edit) { return names(edit, invalidation); }),
                 stack->end());
    if (stack->size() != before) notice_ = "history entries dropped: " + goneText(invalidation);
  }
}

void EditHistory::clear() {
  undo_.clear();
  redo_.clear();
  pending_.clear();
  notice_.clear();
}

std::string_view EditHistory::undoLabel() const { return undo_.empty() ? std::string_view() : undo_.back().label; }

std::string_view EditHistory::redoLabel() const { return redo_.empty() ? std::string_view() : redo_.back().label; }

}  // namespace atlantis::editor
