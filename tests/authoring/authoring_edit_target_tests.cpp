#include "authoring_fixture.h"

#include <atlantis/editor/model/edit_history.h>
#include <atlantis/editor/model/edit_target.h>
#include <atlantis/editor/model/edit_transaction.h>

#include <algorithm>
#include <array>
#include <string>
#include <utility>

// Plan 0065 M3 (P7; Spec 0065 R12, Q7 T-a; ADR-0130 D4, ADR-0124 D4): the
// second edit target, proven and not shipped. A test-only EditTarget over
// AuthoringConnection runs E2's EditHistory unchanged: a recorded authoring
// edit undoes and redoes, a second client's change refuses an undo ("changed
// elsewhere"), and a destroyed node or removed component invalidates its
// entries. The production target belongs to the C# port of E2's models
// (Spec 0064 Phase 2); the frozen ImGui editor gains nothing (ADR-0125 D4).

using namespace authoring_test;
namespace editor = atlantis::editor;

namespace {

// ADR-0124 D4's edit-target interface over AuthoringConnection, settled as
// E2's RuntimeWorldEditTarget settles: applied when the writes'
// PropertyChanged events arrive in order with the written values, refused
// when a failure names the handle. Authoring failures are the authoring
// connection's, so update() drains them itself; the World failures EditHistory
// passes on are not this target's.
class AuthoringEditTarget final : public editor::EditTarget {
 public:
  explicit AuthoringEditTarget(AuthoringConnection& connection) : connection_(connection) {
    ac::EventFilter filter;
    filter.kinds = ac::EventKindSet::only(ac::EventKind::PropertyChanged)
                       .with(ac::EventKind::NodeDestroyed)
                       .with(ac::EventKind::ComponentRemoved);
    subscription_ = connection_.subscribe(filter);
  }
  ~AuthoringEditTarget() override { (void)connection_.unsubscribe(subscription_); }
  AuthoringEditTarget(const AuthoringEditTarget&) = delete;
  AuthoringEditTarget& operator=(const AuthoringEditTarget&) = delete;

  std::optional<ac::PropertyValue> read(const ac::PropertyAddress& address) override {
    auto value = connection_.getProperty(address);
    if (value.isErr()) return std::nullopt;
    return value.value();
  }

  editor::EditHandle submit(std::span<const editor::AddressedValue> writes) override {
    std::vector<ac::Command> commands;
    for (const auto& [address, value] : writes) commands.emplace_back(ac::SetProperty{address, value});
    const wa::TransactionTicket ticket = connection_.submitTransaction(std::move(commands));
    const editor::EditHandle handle{ticket.first.value, ticket.count};
    track(handle, writes);
    return handle;
  }

  void track(editor::EditHandle handle, std::span<const editor::AddressedValue> writes) override {
    tracked_.push_back(Tracked{handle, std::vector<editor::AddressedValue>(writes.begin(), writes.end())});
  }

  editor::Settlement settlement(editor::EditHandle handle) const override {
    for (const Tracked& t : tracked_) {
      if (t.handle == handle) return t.settlement;
    }
    return editor::Settlement::Pending;
  }

  std::string refusalText(editor::EditHandle handle) const override {
    for (const Tracked& t : tracked_) {
      if (t.handle == handle) return t.refusal;
    }
    return {};
  }

  void update(std::span<const wa::CommandFailure> /*worldFailures*/) override {
    std::erase_if(tracked_, [](const Tracked& t) { return t.settlement != editor::Settlement::Pending; });
    for (const ac::CommandFailure& failure : connection_.drainFailures()) {
      for (Tracked& t : tracked_) {
        const std::uint64_t ticket = failure.ticket.value;
        if (t.handle.count != 0 && t.handle.first <= ticket && ticket - t.handle.first < t.handle.count) {
          t.settlement = editor::Settlement::Refused;
          t.refusal = std::string(ac::toString(failure.error));
        }
      }
    }
    auto events = connection_.drainEvents(subscription_);
    REQUIRE(events.isOk());
    for (const ac::Event& event : events.value()) {
      if (const auto* changed = std::get_if<ac::PropertyChanged>(&event)) {
        for (Tracked& t : tracked_) {
          if (t.settlement != editor::Settlement::Pending || t.seen >= t.writes.size()) continue;
          const editor::AddressedValue& next = t.writes[t.seen];
          if (next.first == changed->address && next.second == changed->value && ++t.seen == t.writes.size()) {
            t.settlement = editor::Settlement::Applied;
          }
        }
      } else if (const auto* destroyed = std::get_if<ac::NodeDestroyed>(&event)) {
        invalidations_.push_back(editor::Invalidation{destroyed->node, std::nullopt});
      } else if (const auto* removed = std::get_if<ac::ComponentRemoved>(&event)) {
        invalidations_.push_back(editor::Invalidation{removed->node, removed->component});
      }
    }
  }

  std::vector<editor::Invalidation> drainInvalidations() override { return std::exchange(invalidations_, {}); }

 private:
  struct Tracked {
    editor::EditHandle handle;
    std::vector<editor::AddressedValue> writes;
    std::size_t seen = 0;
    editor::Settlement settlement = editor::Settlement::Pending;
    std::string refusal;
  };

  AuthoringConnection& connection_;
  SubscriptionId subscription_;
  std::vector<Tracked> tracked_;
  std::vector<editor::Invalidation> invalidations_;
};

// An edit made the way E2's Inspector makes one: the writes submitted as one
// transaction through the target, then recorded with their before values.
editor::EditHandle edit(editor::EditTarget& target, editor::EditHistory& history, std::string label,
                        std::vector<editor::PropertyWrite> writes) {
  std::vector<editor::AddressedValue> forward;
  for (const auto& w : writes) forward.emplace_back(w.address, w.after);
  const editor::EditHandle handle = target.submit(forward);
  history.record(editor::EditTransaction{std::move(label), std::move(writes)}, handle);
  history.update({});
  return handle;
}

const ac::PropertyAddress kIntensity = address(kLightNode, kLight, field("Light", "intensity"));
const ac::PropertyAddress kPosition = address(kLightNode, kTransform, field("Transform", "localPosition"));
const ac::PropertyAddress kRotation = address(kLightNode, kTransform, field("Transform", "localEulerAnglesRadians"));

}  // namespace

TEST_CASE("edit target: a recorded authoring edit undoes and redoes through E2's history", "[authoring][edit_target]") {
  Fixture f;
  AuthoringEditTarget target(*f.connection);
  editor::EditHistory history(target);

  edit(target, history, "Set Light.intensity", {{kIntensity, 2.0f, 9.0f}});
  REQUIRE(history.canUndo());
  CHECK(history.undoLabel() == "Set Light.intensity");
  CHECK(f.document().nodes[kLightNode].light->intensity == 9.0f);

  CHECK(history.undo() == editor::HistoryResult::Submitted);
  history.update({});
  CHECK(f.document().nodes[kLightNode].light->intensity == 2.0f);
  CHECK(history.canRedo());
  CHECK(history.redoLabel() == "Set Light.intensity");

  CHECK(history.redo() == editor::HistoryResult::Submitted);
  history.update({});
  CHECK(f.document().nodes[kLightNode].light->intensity == 9.0f);
  CHECK(history.canUndo());
  CHECK_FALSE(history.canRedo());
}

TEST_CASE("edit target: a multi-write edit (a Gizmo drag) undoes as one authoring transaction",
          "[authoring][edit_target]") {
  Fixture f;
  AuthoringEditTarget target(*f.connection);
  editor::EditHistory history(target);
  const SubscriptionId all = f.connection->subscribe({});

  const std::array<float, 3> origin{0.0f, 0.0f, 0.0f};
  const std::array<float, 3> moved{1.0f, 2.0f, 3.0f};
  const std::array<float, 3> turned{0.0f, 0.5f, 0.0f};
  edit(target, history, "Move", {{kPosition, origin, moved}, {kRotation, origin, turned}});
  CHECK(history.undo() == editor::HistoryResult::Submitted);
  history.update({});
  CHECK(f.document().nodes[kLightNode].transform == baseScene().nodes[kLightNode].transform);
  // The undo's two writes arrived as one transaction's events, in order.
  const auto events = f.connection->drainEvents(all).value();
  REQUIRE(events.size() == 4);
  CHECK(events[2] == ac::Event{ac::PropertyChanged{kPosition, origin}});
  CHECK(events[3] == ac::Event{ac::PropertyChanged{kRotation, origin}});
}

TEST_CASE("edit target: a second client's change refuses an undo (changed elsewhere)", "[authoring][edit_target]") {
  Fixture f;
  auto other = f.session.connect();
  AuthoringEditTarget target(*f.connection);
  editor::EditHistory history(target);

  edit(target, history, "Set Light.intensity", {{kIntensity, 2.0f, 9.0f}});
  other->submit(ac::SetProperty{kIntensity, 5.0f});
  REQUIRE(other->drainFailures().empty());
  history.update({});

  CHECK(history.undo() == editor::HistoryResult::ChangedElsewhere);
  CHECK(f.document().nodes[kLightNode].light->intensity == 5.0f);  // nothing submitted
  CHECK(history.undoDepth() == 0);                                 // the entry was dropped
  CHECK_FALSE(history.notice().empty());
}

TEST_CASE("edit target: a destroyed node or a removed component invalidates its entries", "[authoring][edit_target]") {
  SECTION("the node is destroyed") {
    Fixture f;
    auto other = f.session.connect();
    AuthoringEditTarget target(*f.connection);
    editor::EditHistory history(target);
    edit(target, history, "Set Light.intensity", {{kIntensity, 2.0f, 9.0f}});
    REQUIRE(history.undoDepth() == 1);
    other->submit(ac::DestroyNode{entity(3)});
    REQUIRE(other->drainFailures().empty());
    history.update({});
    CHECK(history.undoDepth() == 0);
    CHECK(history.undo() == editor::HistoryResult::NothingTo);
  }
  SECTION("the component is removed, even if it is added back") {
    Fixture f;
    auto other = f.session.connect();
    AuthoringEditTarget target(*f.connection);
    editor::EditHistory history(target);
    edit(target, history, "Set Light.intensity", {{kIntensity, 2.0f, 9.0f}});
    edit(target, history, "Move", {{kPosition, std::array<float, 3>{0.0f, 0.0f, 0.0f}, std::array<float, 3>{1.0f, 0.0f, 0.0f}}});
    REQUIRE(history.undoDepth() == 2);
    other->submitTransaction({ac::RemoveComponent{entity(3), kLight}, ac::AddComponent{entity(3), kLight}});
    REQUIRE(other->drainFailures().empty());
    history.update({});
    CHECK(history.undoDepth() == 1);  // the Light entry went; the Transform entry stays
    CHECK(history.undoLabel() == "Move");
  }
}

TEST_CASE("edit target: an edit the document refuses is not kept", "[authoring][edit_target]") {
  Fixture f;
  AuthoringEditTarget target(*f.connection);
  editor::EditHistory history(target);
  const editor::EditHandle handle = edit(target, history, "Set Light.intensity", {{kIntensity, 2.0f, -1.0f}});
  CHECK(target.settlement(handle) == editor::Settlement::Refused);
  CHECK(target.refusalText(handle) == "FieldOutOfDomain");
  history.update({});
  CHECK_FALSE(history.canUndo());
  CHECK(f.document() == baseScene());
}
