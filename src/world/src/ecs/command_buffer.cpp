#include <atlantis/world/ecs/command_buffer.h>

#include <cstring>
#include <optional>
#include <utility>

#include <atlantis/assert.h>
#include <atlantis/world/ecs/world.h>

namespace atlantis::world::ecs {

// Its address tags the PendingEntity values one buffer issues (Plan 0050 P7).
class CommandBufferIdentity {};

CommandBuffer::CommandBuffer() : identity_(std::make_unique<CommandBufferIdentity>()) {}
CommandBuffer::~CommandBuffer() = default;
CommandBuffer::CommandBuffer(CommandBuffer&&) noexcept = default;
CommandBuffer& CommandBuffer::operator=(CommandBuffer&&) noexcept = default;

PendingEntity CommandBuffer::create() {
  ATLANTIS_CHECK_MSG(identity_ != nullptr, "CommandBuffer::create() called on a moved-from buffer");
  commands_.push_back({Op::Create, PendingEntity{pendingCount_, identity_.get()}, {}, 0});
  return PendingEntity{pendingCount_++, identity_.get()};
}

void CommandBuffer::destroy(CommandTarget target) { commands_.push_back({Op::Destroy, target, {}, 0}); }

void CommandBuffer::record(Op op, CommandTarget target, const ComponentInfo& info, const void* bytes) {
  const std::size_t offset = payload_.size();
  if (bytes != nullptr) {
    payload_.resize(offset + info.size);
    std::memcpy(payload_.data() + offset, bytes, info.size);
  }
  commands_.push_back({op, target, info, offset});
}

ApplyReport CommandBuffer::apply(World& world) {
  ApplyReport report;
  if (world.refuseIfIterating()) {
    for (std::size_t i = 0; i < commands_.size(); ++i) {
      report.failures.push_back({i, EcsError::StructuralChangeDuringQuery});
    }
    return report;  // nothing applied; the buffer is kept for a later apply
  }

  report.created.assign(pendingCount_, kInvalidEntityId);
  for (std::size_t i = 0; i < commands_.size(); ++i) {
    const Command& command = commands_[i];
    if (command.op == Op::Create) {
      report.created[std::get<PendingEntity>(command.target).ordinal] = world.createEntity();
      continue;
    }

    // Resolve the target: an EntityId as recorded, or what a create made.
    std::optional<EntityId> entity;
    if (const auto* id = std::get_if<EntityId>(&command.target)) {
      entity = *id;
    } else {
      const PendingEntity& pending = std::get<PendingEntity>(command.target);
      ATLANTIS_ASSERT_MSG(pending.buffer == identity_.get(),
                          "CommandBuffer: a PendingEntity from another buffer was recorded");
      if (pending.buffer == identity_.get() && pending.ordinal < report.created.size()) {
        entity = report.created[pending.ordinal];
      }
    }
    if (!entity.has_value()) {
      report.failures.push_back({i, EcsError::InvalidEntity});
      continue;
    }

    const void* bytes = payload_.data() + command.payload;
    atlantis::Result<std::monostate, EcsError> result = atlantis::Result<std::monostate, EcsError>::Ok({});
    switch (command.op) {
      case Op::Destroy: result = world.destroyEntity(*entity); break;
      case Op::Add: result = world.addComponent(*entity, command.info, bytes); break;
      case Op::Remove: result = world.removeComponent(*entity, command.info.id); break;
      case Op::Set: result = world.writeComponent(*entity, command.info, bytes); break;
      case Op::Create: break;  // handled above
    }
    if (result.isErr()) report.failures.push_back({i, result.error()});
  }

  commands_.clear();
  payload_.clear();
  pendingCount_ = 0;
  return report;
}

}  // namespace atlantis::world::ecs
