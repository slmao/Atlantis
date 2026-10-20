#pragma once

#include <atlantis/editor/model/edit_target.h>
#include <atlantis/editor/model/edit_transaction.h>
#include <atlantis/world/access/runtime_world_access.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Spec 0063 R3-R6 / ADR-0124 D2, D3 (Plan 0063 P4): the edit history -- this
// editor session's own edits, linear, at most kMaxDepth deep. Undo and redo
// write forward: an undo submits an edit's `before` values as a new
// transaction, a redo its `after` values. Nothing is undoable until the
// transaction that applied it is settled; while any of the history's
// transactions is pending, undo and redo wait (ruling Q3, P-a). Before
// submitting, every address is read and compared (PropertyValue equality)
// with what the edit left there; a difference refuses the step and drops the
// entry (ruling Q4, K-a). An entry naming a destroyed entity or a removed
// component is dropped. UI-free.
namespace atlantis::editor {

enum class HistoryResult : std::uint8_t {
  Submitted,         // the inverse (undo) or forward (redo) transaction was submitted
  NothingTo,         // the stack is empty
  Waiting,           // a history transaction is still pending
  ChangedElsewhere,  // a value differs from what the edit left there: entry dropped, nothing submitted
  Invalidated,       // an address cannot be read any more: entry dropped, nothing submitted
};

// Borrows the target, which must outlive it. Frame thread only; not
// thread-safe.
class EditHistory {
 public:
  static constexpr std::size_t kMaxDepth = 128;

  explicit EditHistory(EditTarget& target);

  // A new edit, applied by `handle` (submitted by its maker). It is pending
  // until update() sees it settled: applied -> undoable, refused -> dropped.
  // Clears the redo stack at once.
  void record(EditTransaction edit, EditHandle handle);

  HistoryResult undo();
  HistoryResult redo();

  // Once per editor frame, with the failures the editor drained this frame:
  // updates the target, settles pending transactions, applies invalidations.
  void update(std::span<const atlantis::world::access::CommandFailure> failures);

  // Forgets everything, pending transactions included (a scene change).
  void clear();

  [[nodiscard]] bool waiting() const noexcept { return !pending_.empty(); }
  [[nodiscard]] bool canUndo() const noexcept { return !waiting() && !undo_.empty(); }
  [[nodiscard]] bool canRedo() const noexcept { return !waiting() && !redo_.empty(); }
  [[nodiscard]] std::size_t undoDepth() const noexcept { return undo_.size(); }
  [[nodiscard]] std::size_t redoDepth() const noexcept { return redo_.size(); }
  // The label of the edit undo() / redo() would act on; empty when none.
  [[nodiscard]] std::string_view undoLabel() const;
  [[nodiscard]] std::string_view redoLabel() const;
  // The last thing the history has to tell the user ("changed elsewhere", a
  // refusal, a dropped entry); empty until there is one.
  [[nodiscard]] const std::string& notice() const noexcept { return notice_; }

 private:
  enum class Step : std::uint8_t { Record, Undo, Redo };
  struct Pending {
    EditTransaction edit;
    EditHandle handle;
    Step step = Step::Record;
    bool invalidated = false;  // an invalidation arrived while pending: drop when settled
    bool redoCleared = false;  // an undo overtaken by a new edit: not redoable once applied
  };

  HistoryResult step(std::deque<EditTransaction>& from, Step kind);
  void push(std::deque<EditTransaction>& stack, EditTransaction edit);
  void invalidate(const Invalidation& invalidation);

  EditTarget& target_;
  std::deque<EditTransaction> undo_;
  std::deque<EditTransaction> redo_;
  std::vector<Pending> pending_;
  std::string notice_;
};

}  // namespace atlantis::editor
