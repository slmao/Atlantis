#pragma once

#include <atlantis/world/ecs/component.h>
#include <atlantis/world/ecs/ecs_error.h>
#include <atlantis/world/ecs/entity_id.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

namespace atlantis::world::ecs {

class World;
class CommandBufferIdentity;  // opaque; private to command_buffer.cpp

// Plan 0050 P7 (Spec 0050 R8): an entity created by a CommandBuffer, before
// apply() gives it an EntityId. Valid only with the buffer that issued it.
struct PendingEntity {
  std::uint32_t ordinal = 0;
  const CommandBufferIdentity* buffer = nullptr;
};

using CommandTarget = std::variant<EntityId, PendingEntity>;

struct CommandFailure {
  std::size_t commandIndex = 0;  // position in recording order
  EcsError error = EcsError::InvalidEntity;
};

struct ApplyReport {
  std::vector<EntityId> created;          // created[i] is pending entity i
  std::vector<CommandFailure> failures;   // in command order
};

// Spec 0050 R8 / ADR-0101 D6: structural changes recorded now and applied
// later, by an explicit apply(world) after any query has finished. Values are
// copied at record time (components are trivially copyable). apply() runs the
// commands in recording order, continues past a failed one and reports each
// failure (Plan 0050 J3), then clears the buffer. While the world is iterating
// it applies nothing, keeps the buffer, and reports every command as
// StructuralChangeDuringQuery (J1). Not thread-safe (ADR-0004).
class CommandBuffer {
 public:
  CommandBuffer();
  ~CommandBuffer();
  CommandBuffer(const CommandBuffer&) = delete;
  CommandBuffer& operator=(const CommandBuffer&) = delete;
  CommandBuffer(CommandBuffer&&) noexcept;
  CommandBuffer& operator=(CommandBuffer&&) noexcept;

  [[nodiscard]] PendingEntity create();
  void destroy(CommandTarget target);

  template <Component T>
  void add(CommandTarget target, const T& value = T{}) {
    record(Op::Add, target, componentInfo<T>(), &value);
  }
  template <Component T>
  void remove(CommandTarget target) {
    record(Op::Remove, target, componentInfo<T>(), nullptr);
  }
  template <Component T>
  void set(CommandTarget target, const T& value) {
    record(Op::Set, target, componentInfo<T>(), &value);
  }

  [[nodiscard]] ApplyReport apply(World& world);

  [[nodiscard]] std::size_t size() const noexcept { return commands_.size(); }
  [[nodiscard]] bool empty() const noexcept { return commands_.empty(); }

 private:
  enum class Op : std::uint8_t { Create, Destroy, Add, Remove, Set };
  struct Command {
    Op op = Op::Create;
    CommandTarget target;
    ComponentInfo info;
    std::size_t payload = 0;  // offset into payload_
  };

  void record(Op op, CommandTarget target, const ComponentInfo& info, const void* bytes);

  std::unique_ptr<CommandBufferIdentity> identity_;
  std::vector<Command> commands_;
  std::vector<std::byte> payload_;
  std::uint32_t pendingCount_ = 0;
};

}  // namespace atlantis::world::ecs
