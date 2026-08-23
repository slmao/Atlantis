#pragma once

namespace atlantis::world {

// See adr/0049-entity-identity-and-handle-invalidation.md,
// adr/0050-transform-hierarchy-composition-and-update-model.md. Every
// public World API that can fail returns exactly one of these -- no
// exception, never undefined behavior for a caller-supplied handle
// mismatch. Checked exhaustively by every switch/if-chain in world.cpp;
// no sixth condition exists anywhere in this module's own algorithms.
enum class WorldError {
  InvalidEntity,          // stale or out-of-range handle, or the invalid sentinel
  WouldCreateCycle,       // setParent() would make child its own ancestor
  NoCameraComponent,      // setActiveCamera() target has no Camera
  WrongWorld,             // handle's identity belongs to a different, live World instance
  NoRenderableComponent,  // getRenderable() target has no Renderable
};

}  // namespace atlantis::world
