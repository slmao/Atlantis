# Plan: World / Scene Foundation

- **Spec:** [specs/0014-world-scene-foundation.md](../specs/0014-world-scene-foundation.md) (`Approved`, Human Review Approval recorded 2026-08-22; carries its own "Accepted Amendment" section, 2026-08-22, stable `World` identity token)
- **Status:** `Approved / Ready for Implementation`. **Implementation is
  authorized by this Plan's own Approval but still gated on
  [PR #67](https://github.com/slmao/Atlantis/pull/67) being merged** —
  see [specs/README.md](../specs/README.md).
- **Author:** Drafted by Claude Code (AI agent) at explicit human
  direction. Reviewed and approved by a human — see Human Review Approval
  below; six Independent Review rounds (condensed below) preceded and fed
  that approval.
- **Human Review Approval (2026-08-22):** Reviewed and approved by slmao
  (`slmao <slmaosjtu@gmail.com>`), accepting this Plan in full as revised
  through Independent Review Rounds 1–4 (Rounds 5–6 are later, additive
  corrections covered by this same approval, per the notes below). This
  approval explicitly covers:
  1. **The full Milestones / Task Breakdown (Steps 1–10)** as revised in
     Round 2 — atomicity narrowed to Step 1's module-skeleton minimum and
     Step 8's mandatory-separate-golden-commit provenance rule; Steps 5
     and 7 left as ordinary sequential work at Implementation's own
     commit-granularity discretion.
  2. **The full Files / Modules Touched scope** — new `src/world/`,
     `tests/world/`, `src/runtime/include/atlantis/runtime/scene_extraction.h`/`.cpp`,
     the new headless fixture/golden-generator/test-case files, and the
     additive-only edits to `runtime_application.{h,cpp}`,
     `init_error.{h,cpp}`, and touched `CMakeLists.txt` files — with
     `src/rhi/`, `src/renderer/`, `src/render_graph/`, `src/vulkan_backend/`,
     `src/platform/`, `src/shader_system/`, `src/asset_system/`, and the
     existing `minimal_cube` golden confirmed untouched.
  3. **`World`'s full move/ownership contract** — move-constructible, not
     copyable, not move-assignable; move-construction transfers the
     identity token and all state together; a moved-from `World`
     guarantees only that it remains destructible or may be
     move-constructed from again, with every other call an
     `ATLANTIS_CHECK`-guarded programmer error.
  4. **The stable `World` identity token design in full** — `EntityId`'s
     three-field shape, **all three fields `private`** (index, generation,
     and a non-owning identity reference; read-only
     `index()`/`generation()` accessors where a real call site needs
     them, no accessor for identity, no mutator for any field — see the
     2026-08-23 correction), the new `WorldError::WrongWorld` enumerator,
     identity-before-slot/generation validation ordering via the shared
     `validate()` helper, and the explicit prohibition on serializing,
     persisting, or using `EntityId` across a process boundary — matching
     ADR-0049's and Spec 0014's own Accepted Amendments item-for-item.
  5. **Runtime's own adapter** (`scene_extraction.h`/`.cpp`, the two new
     `RuntimeInitError` enumerators, `RuntimeApplication`'s new members
     and replaced `runFrame()` Step 7) exactly as D6–D9 specify.
  6. **The headless golden's own ADR-0042 "Initial baseline bootstrap"
     provenance procedure** (D10) — code committed first against a clean
     tree, golden capture as its own separate, subsequent commit, full
     provenance sidecar fields, substitute evidence in place of an
     inapplicable old-vs-new diff.
  7. **The full V1–V28 Verification Checklist**, including the six
     verifications added across Rounds 3–5 for the identity-token design
     (V22–V27) alongside the original V1–V21, plus V28 (Round 6,
     2026-08-23) for the `WorldError::NoRenderableComponent` correction.

  This approval does not authorize Implementation to begin immediately —
  [PR #67](https://github.com/slmao/Atlantis/pull/67) (carrying this Plan
  and its full review history) must be merged first.

  **Correction (2026-08-23), covered by this same approval, no new review
  round:** item 4 above is refined so all three of `EntityId`'s fields
  (index, generation, identity) are `private`, not only identity — closes
  a same-`World` forgery loophole a plain public `index`/`generation`
  would otherwise have left.

  **Human Review Correction (2026-08-23), covered by this same approval,
  no new review round:** Implementation disclosed that
  `World::getRenderable()` had no `WorldError` enumerator of its own for a
  valid entity with no `Renderable`, and reused `NoCameraComponent` as an
  interim choice. Human Review **rejected** that reuse. `WorldError` gains
  a fifth enumerator, `NoRenderableComponent`, restoring symmetry with
  `NoCameraComponent`; validation priority is unchanged. This correction
  is governance-only: it does not itself modify
  [PR #68](https://github.com/slmao/Atlantis/pull/68)'s Implementation
  code, and does not by itself authorize merging that PR — V20 (genuine
  human, GUI-based visual confirmation) was a separate gate, subsequently
  satisfied 2026-08-23 (see "Post-Merge Status Update" in Deviations).
- **Historical scope — Independent Review, six rounds:** Round 1
  (2026-08-22) read `src/runtime/`, `src/renderer/`, `src/asset_system/`,
  `assets/CMakeLists.txt`, `tests/image_regression/`, and
  ADR-0042's own Accepted Amendment in full against `main`'s real, current
  source tree, resolving how Runtime obtains `minimal_cube`'s real
  `AssetId` (via the already-public `parseAssetMetadata()`, D6) and
  finding — but not solving — that `EntityId` carried no `World`-instance
  identity. Round 2 resolved six points directly (`World` copy/move
  semantics; a fully iterative, heap-allocated `updateTransforms()`
  replacing recursion to remove an unbounded call-stack risk;
  re-verification that childless cascading destroy has no bug; that
  camera degenerate-input coverage was already adequate; that golden
  commit ordering was already correct; narrowing three over-broad "atomic"
  step labels to their genuine mechanical minimum) and escalated the
  seventh — cross-`World`-instance `EntityId` use — to Human Review as a
  real gap in the `Accepted` contract, not a Plan-stage detail. Round 3
  applied Human Review's direction (a stable, per-`World`, heap-allocated,
  address-stable identity token — explicitly not a global counter) to
  D2–D5/D11 and V22–V25. Round 4 applied two further Human-Review-added
  refinements (the identity field made `private`; an explicit
  moved-from-`World` contract, V26) and recorded formal Amendment Approval
  on both ADR-0049 and Spec 0014. Round 5 (2026-08-23) found Round 4's
  "private identity field" refinement had left `index`/`generation` as
  plain public fields — a same-`World` forgery loophole — and made all
  three fields private (V27). Round 6 (2026-08-23) added
  `WorldError::NoRenderableComponent` per the Human Review Correction
  above (V28). See [PR #67](https://github.com/slmao/Atlantis/pull/67) for
  the full revision history.
- **Related ADR(s):**
  [ADR-0048](../adr/0048-world-scene-module-boundary-and-ownership.md)–[ADR-0051](../adr/0051-world-to-renderer-extraction-and-asset-resolution-boundary.md),
  all `Accepted` 2026-08-22, **including** ADR-0049's "Accepted Amendment"
  section (2026-08-22, stable `World` identity token).
- **Editorial revision:** [Spec 0033](../specs/0033-documentation-lifecycle-and-compaction.md);
  [PR #151](https://github.com/slmao/Atlantis/pull/151) Batch 4. Original scope, D1–D11, the Milestones/Task
  Breakdown, and the full V1–V28 matrix retained; the six-round Independent
  Review narration is condensed above, with the full drafts preserved in
  [PR #67](https://github.com/slmao/Atlantis/pull/67) history.

## Objective

Implement Spec 0014 in full: a new, eleventh top-level module,
`Atlantis::World`, giving Atlantis its first in-memory, multi-entity
scene — Entity lifecycle with a formally-overflow-safe index+generation
handle, fixed-type `Transform`/`Camera`/`Renderable` component storage, an
atomic parent/child hierarchy with cycle prevention and cascading
destroy, and a fully-specified TRS/matrix math contract robust to a
sheared hierarchy — then extend `Atlantis::RuntimeHost`'s existing
bootstrap composition (Spec 0013, unchanged in its own object model and
lifecycle) with a Runtime-owned, Runtime-private adapter that turns a
World-driven, six-entity validation scene (five `Renderable` cube
instances, one hierarchy relationship, one `Camera`) into the exact same,
**unmodified** `atlantis::renderer::DrawItem`/`Renderer::drawFrame()`
inputs every existing composition root already uses. A new headless
image-regression fixture and golden prove the extraction pixel-correct;
the existing windowed `atlantis_runtime` gains a GPU smoke test and a
manual verification pass, exactly matching Spec 0013's own established
three-layer verification model.

## Plan-level decisions (fixed here, not left to Implementation)

### D1. CMake targets, namespace, directories, dependency graph

| Target | Kind | Location | Links | Notes |
|---|---|---|---|---|
| `atlantis_world` (alias `Atlantis::World`) | STATIC | `src/world/` | **PUBLIC** `Atlantis::Core`, **PUBLIC** `Atlantis::AssetSystem`; PRIVATE `atlantis_compiler_warnings` | `Atlantis::AssetSystem` is **PUBLIC**, not PRIVATE: `Renderable`'s own public header names `atlantis::asset_system::AssetId` in a field type, so any consumer of `<atlantis/world/renderable.h>` must transitively see that type — matching how `Atlantis::ShaderSystemRhiIntegration` already links `Atlantis::RHI` PUBLIC for the identical reason. No other dependency, in either direction — verified by a module-boundary test mirroring `tests/asset_system/module_boundary_tests.cpp` (V16). |
| `atlantis_world_tests` | executable | `tests/world/` | `Atlantis::World`, `Catch2::Catch2WithMain`, `atlantis_compiler_warnings` | GPU-independent — links no RHI/Renderer/Vulkan Backend/Platform target at all. |

- **Namespace:** `atlantis::world`. **Public header root:**
  `src/world/include/atlantis/world/`.
- **One primary type per header/source pair:** `vec3.h`, `entity_id.h`,
  `world_error.h`, `transform.h`, `camera.h`, `renderable.h` are each a
  single small value type with no `.cpp` of their own; `world.h`/`world.cpp`
  hold the `World` class itself, the only type in this module with real
  behavior.
- **Root `CMakeLists.txt` ordering:** `add_subdirectory(src/world)`
  inserted immediately after `add_subdirectory(src/asset_system)` and
  before `add_subdirectory(src/platform)` — the earliest point both of
  `Atlantis::World`'s own dependencies are already declared, matching this
  repository's own established convention. `add_subdirectory(tests/world)`
  inserted immediately after `add_subdirectory(tests/asset_system)` in the
  `ATLANTIS_BUILD_TESTS` block. `add_subdirectory(src/runtime)` (already
  present, later in the file) needs no ordering change — `src/world` lands
  well before it.

### D2. Public value types — exact C++ shapes

Each header below is the **entire** contents of its own module's public
contract for that type, per Spec 0014's Requirements and ADR-0049/0050 as
amended by ADR-0049's own Accepted Amendment (stable `World` identity
token; Human Review Amendment Approval 2026-08-22):

```cpp
// vec3.h
struct Vec3 { float x = 0.0f; float y = 0.0f; float z = 0.0f; };

// entity_id.h
namespace atlantis::world {
class World;           // forward declaration (friend)
class WorldIdentity;   // opaque forward declaration only -- full definition
                        // private to world.cpp; EntityId never dereferences it

struct EntityId {
  EntityId() = default;

  [[nodiscard]] std::uint32_t index() const noexcept { return index_; }
  [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }

  friend bool operator==(const EntityId&, const EntityId&) = default;

 private:
  friend class World;  // only World may construct a non-default EntityId
                        // or read any of its three private fields
  EntityId(std::uint32_t idx, std::uint64_t gen, const WorldIdentity* identity)
      : index_(idx), generation_(gen), worldIdentity_(identity) {}

  std::uint32_t index_ = std::numeric_limits<std::uint32_t>::max();
  std::uint64_t generation_ = 0;
  const WorldIdentity* worldIdentity_ = nullptr;
};
inline constexpr EntityId kInvalidEntityId{};  // index_ == max, generation_ == 0, worldIdentity_ == nullptr
}  // namespace atlantis::world

// world_error.h
enum class WorldError {
  InvalidEntity,          // stale or out-of-range handle, or the invalid sentinel
  WouldCreateCycle,       // setParent() would make child its own ancestor
  NoCameraComponent,      // setActiveCamera() target has no Camera
  WrongWorld,             // handle's identity belongs to a different, live World instance
  NoRenderableComponent,  // getRenderable() target has no Renderable (2026-08-23 correction)
};

// transform.h
struct Transform {
  Vec3 localPosition{};
  Vec3 localEulerAnglesRadians{};   // pitch (x), yaw (y), roll (z)
  Vec3 localScale{1.0f, 1.0f, 1.0f};
};

// camera.h
struct Camera { float fovYRadians = 0.0f; float nearZ = 0.0f; float farZ = 0.0f; };

// renderable.h
struct Renderable { atlantis::asset_system::AssetId meshAsset = 0; };
```

- `EntityId`'s default constructor already equals `kInvalidEntityId` — the
  named constant exists purely for readability at call sites.
- **`EntityId` is 24 bytes on a typical 64-bit target** (`uint32_t` index
  padded to 8, `uint64_t` generation, one pointer) — up from the prior
  16-byte shape; the *exact* figure is target-pointer-width- and
  alignment-dependent and is not a number this Plan fixes.
- **All three of `EntityId`'s own fields — `index_`, `generation_`,
  `worldIdentity_` — are `private`, not only the identity field**
  (corrected 2026-08-23; a prior drafting pass left `index`/`generation`
  public, which would have let a caller copy a legitimate handle and
  directly overwrite its index, coincidentally forging a *different*
  entity within the *same* `World` whenever the mutated index carried a
  matching generation — the private identity token alone does not prevent
  this, since `validate()` checks index/generation against `slots_`
  regardless of identity). Only `World` (a `friend`) may construct an
  `EntityId` carrying non-default values, via the `private` three-argument
  constructor; no other code can read, forge, or overwrite any of the
  three fields. A caller can still hold, copy, and compare an `EntityId`
  freely — the defaulted `operator==` compares all three members
  regardless of access level — and can read `index()`/`generation()`
  through the read-only accessors where a real call site needs them, but
  cannot construct or mutate an `EntityId` with an arbitrary index,
  generation, or identity. **No accessor of any kind exists for
  `worldIdentity_`, and no mutator of any kind exists for any of the three
  fields.** Declaring these ordinary member functions does not affect
  `std::is_trivially_copyable<EntityId>` (which examines only the
  copy/move constructors, copy/move assignment, and destructor — all four
  remain implicitly generated and trivial) — `EntityId` stays freely
  copyable, safe to store in `std::vector`. `worldIdentity_` remains a
  plain observer pointer — `EntityId` never allocates, frees, or
  dereferences anything through it.
- **`EntityId` must never be serialized, persisted, or used across a
  process boundary** — its identity component is a heap address,
  meaningful only within the process and `World` instance that produced
  it.
- **`WorldError` gains a fifth enumerator, `NoRenderableComponent`**
  (Human Review Correction, 2026-08-23): `getRenderable()` returns it for
  an otherwise-valid entity with no `Renderable`, in place of the
  disclosed Implementation interim choice of reusing `NoCameraComponent`.
  `NoCameraComponent` is unchanged — a missing `Camera` component only.

### D3. `World`'s internal slot map — exact representation

Private to `world.cpp` (not declared in any public header):

```cpp
struct WorldIdentity {};  // opaque, no data, no behavior -- only its own
                           // heap address matters, as this instance's
                           // unique, address-stable token

enum class SlotVisitState : std::uint8_t { NotVisited, Visiting, Visited };  // transient, updateTransforms() only

struct Slot {
  bool alive = false;
  std::uint64_t generation = 0;
  EntityId parent = kInvalidEntityId;
  Transform localTransform;
  std::array<float, 16> cachedWorldMatrix = kIdentityMatrix4;  // valid only after updateTransforms()
  std::optional<Camera> camera;
  std::optional<Renderable> renderable;
  SlotVisitState visitState = SlotVisitState::NotVisited;  // reset at the start of every updateTransforms() call
};

class World {
 public:
  World();   // defined in world.cpp -- allocates identity_ via
             // std::make_unique<WorldIdentity>(), which requires
             // WorldIdentity's complete definition, private to this file
  ~World();  // defined in world.cpp for the identical reason
  World(const World&) = delete;
  World& operator=(const World&) = delete;
  World(World&&) noexcept = default;  // may stay inline: moving a
                                        // unique_ptr<Incomplete> does not
                                        // require completeness
  World& operator=(World&&) = delete;
  // ... rest of public API (D4/D5) ...
 private:
  std::unique_ptr<WorldIdentity> identity_;  // this instance's own stable token
  std::vector<Slot> slots_;
  std::vector<std::uint32_t> freeList_;      // LIFO stack: push_back()/pop_back() only
  std::optional<EntityId> activeCamera_;
};
```

**Why `world.h` only forward-declares `WorldIdentity`:** `entity_id.h`
also only forward-declares it — neither header needs the complete type,
since both only ever hold or compare a pointer to it. The one-line
definition lives in `world.cpp` alone. This is the standard C++ idiom for
an opaque-pointer member; it is what forces `World`'s own constructor and
destructor to be declared in the header but **defined** in `world.cpp`
(`= default` bodies suffice for both), since `std::make_unique<WorldIdentity>()`
and `unique_ptr`'s implicit `delete` both require `WorldIdentity` to be
complete at the point they are compiled.

**`World`'s own copy/move semantics — now directly load-bearing for
identity, not merely a convenience choice** (ADR-0049's Accepted
Amendment). Move-constructible, **not** move-assignable, **not**
copyable — the same "move-construction-only" shape
`PlatformSession`/`RuntimeApplication` already establish:

- **Not copyable.** A copy constructor would have to choose between
  sharing the source's own `identity_` token (defeating the entire
  point — two live `World`s would then validate the same handles) or
  minting a fresh one (which would make every `EntityId` copied over
  silently `Err(WrongWorld)` against the copy). No choice is right, so
  copying is deleted outright.
- **Move-constructible.** `RuntimeApplication` holds `world_` as a plain
  value member (D8) and is itself move-constructible (Spec 0013's
  already-`Accepted` pattern) — its compiler-generated move constructor
  requires every member, `World` included, to be move-constructible.
  Moving `identity_` (a `std::unique_ptr`) transfers ownership of the
  **same** underlying `WorldIdentity` heap block — that object's own
  address never changes across the move. Together, `identity_.get()`
  returns the **exact same address** before and after the move, and
  `slots_`/`freeList_`/`activeCamera_` are preserved exactly — so any
  `EntityId` valid before the move remains valid after it.
- **Not move-assignable.** Move-assignment would free `worldA`'s own
  `identity_` token and replace it with `worldB`'s, while any `EntityId` a
  caller still holds against "the `World` that used to be reachable as
  `worldA`" remains stamped with the now-freed token's own (dangling)
  address — a real lifetime hazard, not solved by `WrongWorld` (which only
  helps between two *simultaneously live* instances). `RuntimeApplication`
  itself does not need `World`'s move-assignment (its own is already
  deleted).
- A `static_assert`-based test confirms exactly this shape:
  `std::is_move_constructible_v<World> && !std::is_copy_constructible_v<World>
  && !std::is_move_assignable_v<World> && !std::is_copy_assignable_v<World>`
  (V22).

**Moved-from `World`: only destructible or move-constructible-from again —
every other call is a programmer error, checked, not silently tolerated**
(ADR-0049's Accepted Amendment). After `World b = std::move(a);`,
`a.identity_` becomes `nullptr` — `std::unique_ptr`'s guaranteed
moved-from state, the exact, already-available signal `World` uses to
detect this. `identity_ != nullptr` is asserted (`ATLANTIS_CHECK_MSG`) at
the start of every public method **except** the constructor, destructor,
and move constructor. Every `EntityId`-accepting method gets this for
free via `validate()`; `createEntity()`, `updateTransforms()`,
`clearActiveCamera()`, and `activeCamera()` each perform the identical
check directly at entry (D4). New verification: V26.

- **`validate(id)` — private helper, identity and moved-from state checked
  before slot/generation, every `EntityId`-accepting method routes through
  it:**

  ```cpp
  Result<void, WorldError> World::validate(EntityId id) const {
    ATLANTIS_CHECK_MSG(identity_ != nullptr,
                        "World::validate() called on a moved-from World");
    if (id.worldIdentity_ != nullptr && id.worldIdentity_ != identity_.get())
      return Result<void, WorldError>::Err(WorldError::WrongWorld);
    if (id.index_ >= slots_.size() || !slots_[id.index_].alive
        || slots_[id.index_].generation != id.generation_)
      return Result<void, WorldError>::Err(WorldError::InvalidEntity);
    return Result<void, WorldError>::Ok({});
  }
  ```

  The `id.worldIdentity_ != nullptr` guard is deliberate: `kInvalidEntityId`
  (`worldIdentity_ == nullptr`) is "no claimed identity," not "a foreign
  instance's identity" — without the guard, the sentinel would incorrectly
  report `WrongWorld` instead of `InvalidEntity`. A real handle from a
  **different, live** `World` (non-null, non-matching pointer) correctly
  reaches `WrongWorld` before either the index or generation is inspected.
- **`createEntity()`:** `ATLANTIS_CHECK_MSG(identity_ != nullptr, ...)`
  first (moved-from guard). If `freeList_` is non-empty, `pop_back()` its
  index (LIFO); otherwise `slots_.emplace_back()` and use the new highest
  index. Set `alive = true`, reset `parent`/`localTransform`/`camera`/
  `renderable` to default (generation is **not** touched here — it was
  already advanced by `destroyEntity()`), return `EntityId{index,
  slots_[index].generation, identity_.get()}`. Never fails.
- **`destroyEntity(id)` — cascading, via a worklist, no stored children
  list:** `validate(id)` first. On success, build `std::vector<EntityId>
  toDestroy{id}` and, for each entry already in the list (index-based loop
  so appending during iteration is safe), scan every **alive** slot whose
  `parent` equals that entry and append it. **Only after this collection
  phase is fully complete** does a second loop mutate any slot: for every
  entity in `toDestroy` (order immaterial): if `activeCamera_` equals it,
  clear it; mark the slot dead; increment `generation`; if the
  post-increment value is **not** the tombstone, `freeList_.push_back(index)`
  — if it **is**, the index is never pushed, permanently retiring it.

  **Correctness of this two-phase (collect-then-mutate) structure,
  verified against three specific hazards (miss a descendant, process one
  twice, slot reuse changing the set mid-scan):** the hierarchy is a
  forest — `setParent()`'s own cycle check (D5) guarantees each entity has
  exactly one parent and no entity is its own ancestor — so every
  descendant is reachable by exactly one path, and the index-based `for`
  loop (re-evaluating `toDestroy.size()` each iteration) visits every
  entry ever appended, at every depth. No double-processing: an entity has
  exactly one parent, so it can be discovered at most once. Slot reuse
  cannot change the set mid-scan: `current = toDestroy[i]` copies the
  `EntityId` by value before any further work; more fundamentally, no slot
  is mutated until collection has finished, and this Spec's own
  single-threaded model precludes any interleaved `createEntity()`/
  `destroyEntity()`.
- **Generation-retirement tombstone, exact check:** after
  `++slot.generation`, `if (slot.generation ==
  std::numeric_limits<std::uint64_t>::max()) { /* retired; do not push to
  freeList_ */ }` — a single equality check, no separate "near the edge"
  threshold.
- **`isValid(id)`:** `ATLANTIS_CHECK_MSG(identity_ != nullptr, ...)` first,
  then `(id.worldIdentity_ == nullptr || id.worldIdentity_ ==
  identity_.get()) && id.index_ < slots_.size() &&
  slots_[id.index_].alive && slots_[id.index_].generation ==
  id.generation_` — the same checks `validate()` performs, collapsed to a
  `bool`.

### D4. Component and Transform accessor API — exact signatures, atomic by construction

Every `EntityId`-accepting method — `destroyEntity()`, every setter/getter
below, and `setParent()`/`getParent()` (D5) — routes through the same
`validate(id)` helper **first**: moved-from checked, then identity checked
before slot/generation, `Err(WrongWorld)` before `Err(InvalidEntity)`.
Every setter additionally validates — and, for `setParent()`, runs the
cycle check — **before** writing any member, so a `Result::Err` return is
always zero-mutation by construction; no explicit "rollback" code exists
anywhere. `updateTransforms()`, `clearActiveCamera()`, and
`activeCamera()` — accepting no `EntityId` — each perform the identical
moved-from check directly at entry (V26 exercises all of these plus an
arbitrary `EntityId`-accepting method via `validate()`).

```cpp
class World {
 public:
  [[nodiscard]] EntityId createEntity();
  [[nodiscard]] atlantis::Result<void, WorldError> destroyEntity(EntityId id);
  [[nodiscard]] bool isValid(EntityId id) const noexcept;

  [[nodiscard]] atlantis::Result<void, WorldError> setParent(EntityId child, EntityId parent);
  [[nodiscard]] atlantis::Result<EntityId, WorldError> getParent(EntityId child) const;

  [[nodiscard]] atlantis::Result<void, WorldError> setLocalTransform(EntityId id, Transform transform);
  [[nodiscard]] atlantis::Result<Transform, WorldError> getLocalTransform(EntityId id) const;

  void updateTransforms();
  [[nodiscard]] atlantis::Result<std::array<float, 16>, WorldError> getWorldMatrix(EntityId id) const;

  [[nodiscard]] atlantis::Result<void, WorldError> setCamera(EntityId id, Camera camera);
  [[nodiscard]] atlantis::Result<void, WorldError> removeCamera(EntityId id);
  [[nodiscard]] atlantis::Result<Camera, WorldError> getCamera(EntityId id) const;

  [[nodiscard]] atlantis::Result<void, WorldError> setActiveCamera(EntityId id);
  void clearActiveCamera() noexcept;
  [[nodiscard]] std::optional<EntityId> activeCamera() const noexcept;

  [[nodiscard]] atlantis::Result<void, WorldError> setRenderable(EntityId id, Renderable renderable);
  [[nodiscard]] atlantis::Result<void, WorldError> removeRenderable(EntityId id);
  [[nodiscard]] atlantis::Result<Renderable, WorldError> getRenderable(EntityId id) const;

  // Ascending slot-index order -- a fresh std::vector snapshot each call,
  // valid as of the call, not a live iterator held across a subsequent
  // World mutation.
  [[nodiscard]] std::vector<EntityId> renderableEntities() const;
};
```

- **`getParent(child)`** returns `Ok(kInvalidEntityId)` for a root entity
  — not an error.
- **`setActiveCamera(id)`** runs `validate(id)` first then checks
  `slots_[id.index_].camera.has_value()` (`NoCameraComponent`) before
  writing `activeCamera_ = id`.
- **`getRenderable(id)`** (Human Review Correction, 2026-08-23) runs
  `validate(id)` first then checks `slots_[id.index_].renderable.has_value()`,
  returning `Err(NoRenderableComponent)` — never `NoCameraComponent` —
  when absent, matching `setActiveCamera()`'s own priority ordering.
- **`renderableEntities()`** — no `EntityId` argument, but reads
  `identity_.get()` to stamp every result, so it performs the same
  moved-from check directly at entry — walks `slots_` in ascending index
  order, appending `EntityId{index, slot.generation, identity_.get()}`
  for every slot with `alive && renderable.has_value()`.

### D5. `setParent()` — cycle check, exact algorithm

```cpp
Result<void, WorldError> World::setParent(EntityId child, EntityId parent) {
  if (auto r = validate(child); r.isErr()) return r;
  if (parent != kInvalidEntityId) {
    if (auto r = validate(parent); r.isErr()) return r;
    // Walk parent's own ancestor chain, including parent itself as the
    // zeroth step -- this single loop covers both "parent == child" and
    // every longer transitive cycle with one algorithm.
    EntityId ancestor = parent;
    while (ancestor != kInvalidEntityId) {
      if (ancestor == child) return Result<void, WorldError>::Err(WorldError::WouldCreateCycle);
      ancestor = slots_[ancestor.index_].parent;
    }
  }
  slots_[child.index_].parent = parent;
  return Result<void, WorldError>::Ok({});
}
```

No state is written until the function's final line — every `Err` path
returns before any mutation.

### D6. Math contract — exact matrix construction, and how Runtime obtains `minimal_cube`'s real `AssetId`

**Matrix layout and composition** (ADR-0050's Math contract, restated as
concrete code): column-major `std::array<float, 16>`, index `col * 4 +
row`; `kIdentityMatrix4 = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}`;
`multiply(a, b)` implements `result = a · b` for that layout — **the exact
same element formula** `minimal_renderer_demo/main.cpp`'s own `multiply()`
already uses, reused verbatim as `atlantis::world`'s own private
implementation.

**Per-axis rotation matrices** (right-handed, column-vector, matching
ADR-0050's own stated `Ry` example):

```cpp
Mat4 rotationX(float theta) { const float c = std::cos(theta), s = std::sin(theta);
  return {1,0,0,0,  0,c,s,0,  0,-s,c,0,  0,0,0,1}; }
Mat4 rotationY(float theta) { const float c = std::cos(theta), s = std::sin(theta);
  return {c,0,-s,0,  0,1,0,0,  s,0,c,0,  0,0,0,1}; }
Mat4 rotationZ(float theta) { const float c = std::cos(theta), s = std::sin(theta);
  return {c,s,0,0,  -s,c,0,0,  0,0,1,0,  0,0,0,1}; }
Mat4 eulerRotation(Vec3 radians) {  // R = Ry(yaw) * Rx(pitch) * Rz(roll)
  return multiply(rotationY(radians.y), multiply(rotationX(radians.x), rotationZ(radians.z))); }
Mat4 composeLocal(const Transform& t) {  // local = T * R * S
  return multiply(translationMatrix(t.localPosition),
                   multiply(eulerRotation(t.localEulerAnglesRadians), scaleMatrix(t.localScale))); }
```

`translationMatrix`/`scaleMatrix` are the obvious identity-plus-one-field
variants, fixed in shape by the comment above.

**`updateTransforms()` — memoized, fully iterative traversal (no C++
recursion, no call-stack depth tied to hierarchy depth), doubling as the
defense-in-depth cycle guard.** Plan Review Round 2 replaced the original
per-entity memoized *recursion* (one C++ call-stack frame per
ancestor-chain level) with an iterative one, since the original design's
call-stack usage was O(hierarchy depth) with no stated or enforced
bound — a real robustness gap, since `World` places no limit on `setParent()`
nesting depth. The traversal's own internal order was never part of
World's public contract (only "parent before child" is), so replacing
recursion with an iterative algorithm producing the same topological order
changes no `Accepted` decision.

```cpp
void World::updateTransforms() {
  ATLANTIS_CHECK_MSG(identity_ != nullptr, "World::updateTransforms() called on a moved-from World");
  for (auto& slot : slots_) slot.visitState = SlotVisitState::NotVisited;
  std::vector<std::uint32_t> path;  // reused scratch buffer -- heap-allocated, not call-stack depth
  for (std::uint32_t i = 0; i < slots_.size(); ++i) {
    if (!slots_[i].alive || slots_[i].visitState == SlotVisitState::Visited) continue;
    // Walk up from i, collecting the not-yet-visited prefix of its own
    // ancestor chain, stopping at a root or at an already-Visited ancestor.
    path.clear();
    std::uint32_t current = i;
    while (true) {
      Slot& s = slots_[current];
      if (s.visitState == SlotVisitState::Visited) break;
      ATLANTIS_CHECK_MSG(s.visitState != SlotVisitState::Visiting,
                          "updateTransforms(): cycle detected -- setParent()'s own prevention has a bug");
      s.visitState = SlotVisitState::Visiting;
      path.push_back(current);
      if (s.parent == kInvalidEntityId) break;
      current = s.parent.index_;
    }
    // Process outermost-unvisited-ancestor-first, so each entity's own
    // parent world matrix is already known -- an ordinary loop, no recursion.
    for (auto it = path.rbegin(); it != path.rend(); ++it) {
      Slot& s = slots_[*it];
      const std::array<float, 16> local = composeLocal(s.localTransform);
      s.cachedWorldMatrix = (s.parent == kInvalidEntityId)
          ? local
          : multiply(slots_[s.parent.index_].cachedWorldMatrix, local);
      s.visitState = SlotVisitState::Visited;
    }
  }
}
```

`path` grows on the heap (reused across outer iterations), so its size is
bounded only by available memory, not the C++ call stack. No depth cap is
introduced. Any traversal order satisfying "visit a parent before its
child" produces identical results.

**How Runtime obtains `minimal_cube`'s real `AssetId`.**
`StaticMeshAssetData` (returned by `loadStaticMeshAsset()`) carries no
`AssetId` field, but Asset System's already-public `asset_metadata.h`
does: `parseAssetMetadata(std::string_view) -> Result<AssetMetadata,
MetadataParseError>`, and `AssetMetadata::assetId` is exactly the real,
cooked `AssetId`. Runtime therefore reads `config.assetMetadataPath`'s own
file contents as plain text (a second, small read of the same file
`loadStaticMeshAsset()` already consumes) and calls `parseAssetMetadata()`
on it directly, storing the result in a new `RuntimeApplication` member,
`knownMinimalCubeAssetId_`. **No change to any Asset System header, `.cpp`,
or `CMakeLists.txt`** — both functions already exist, `Accepted`,
unmodified.

### D7. Runtime's own camera-matrix extraction and asset resolution — new, independently-testable files

A new file pair, `scene_extraction.h`/`.cpp`, factors two pieces of pure,
GPU-independent logic out of `runtime_application.cpp`'s own anonymous
namespace so they are unit-testable (`tests/runtime/`, "no `Device`, no
GPU, no `World` instance required"). This is a **refactor of Runtime's own
private helpers**, not a new public API surface, and not a shared
"Extraction module" between Runtime and the new image-regression fixture
— ADR-0051's own Alternatives Considered already forecloses that; the new
fixture (D10) duplicates this same logic independently, matching this
codebase's established "duplicated, not shared" precedent.

```cpp
// scene_extraction.h
namespace atlantis::runtime {
using Mat4 = std::array<float, 16>;

enum class SceneExtractionError {
  NoActiveCamera,
  DegenerateCameraForward,   // column 2's own world-space image has near-zero length
  DegenerateCameraBasis,     // forward is (near-)parallel to the canonical world-up axis
  UnresolvedMeshAsset,       // a Renderable's AssetId matches no known, resolved asset
};

struct CameraMatrices { Mat4 view; Mat4 projection; };

// Extracts eye+forward ONLY from cameraWorldMatrix (never a right/up
// column), feeds them into the same lookAt()-shaped construction every
// existing composition root already uses, and builds the projection
// matrix from fovYRadians/nearZ/farZ and the caller-supplied aspect
// ratio. Detects both degenerate-input cases explicitly
// (kDegenerateLengthEpsilon = 1e-6f, chosen because this codebase's own
// scenes operate at a roughly 1-10 world-unit scale) before ever calling
// lookAt(), so that function is never invoked with a near-degenerate input.
[[nodiscard]] atlantis::Result<CameraMatrices, SceneExtractionError> extractCameraMatrices(
    const Mat4& cameraWorldMatrix, float fovYRadians, float nearZ, float farZ, float aspect);

// Trivial by design (ADR-0051's Decision step 4 fixes only the existence
// and input/output shape of asset resolution, not a container) -- this
// Plan's own validation scene resolves against exactly one known AssetId.
[[nodiscard]] atlantis::Result<std::monostate, SceneExtractionError> resolveMeshAsset(
    atlantis::asset_system::AssetId requested, atlantis::asset_system::AssetId known);
}  // namespace atlantis::runtime
```

`extractCameraMatrices()`'s own internal steps: (1) `forward =
normalize(-column 2 of cameraWorldMatrix)`; if the pre-normalization
length is `< kDegenerateLengthEpsilon`, `Err(DegenerateCameraForward)`.
(2) `eye = column 3` (translation). (3) `right = cross(forward,
{0,1,0})`; if its own pre-normalization length is `< kDegenerateLengthEpsilon`,
`Err(DegenerateCameraBasis)` — this check runs **before** calling
`lookAtMatrix()`, which performs the identical cross product internally
without a guard. (4) On success, `view = lookAtMatrix(eye, eye+forward)`,
`projection = perspectiveMatrix(fovYRadians, aspect, nearZ, farZ)`.
`resolveMeshAsset()` is `requested == known ? Ok(std::monostate{}) :
Err(UnresolvedMeshAsset)` — one comparison against the single known
`AssetId` D8 describes.

### D8. `RuntimeApplication` — new members, and per-frame extraction order

**New members** (added alongside `renderer_`/`lifecycle_`; none of it
participates in the fixed reverse-destruction-order sequence, since
`World` owns no GPU resource):

```cpp
atlantis::world::World world_;
atlantis::asset_system::AssetId knownMinimalCubeAssetId_ = 0;
std::optional<atlantis::world::EntityId> activeCameraEntity_;  // cached for logging only; World itself is the source of truth
```

**`initializeSteps()` gains one new step**, after the existing Step 4
(asset load) and before Step 5 (mesh creation) — reading the metadata a
second time for its `AssetId` (D6): on `parseAssetMetadata()` failure, the
same teardown-and-exit path Step 4's own failure uses, returning a **new**
`RuntimeInitError::AssetMetadataParseFailed`. After the existing Step 6
(camera buffer) and before `lifecycle_.markRunning()`, a new step
**builds the fixed validation scene** (D9) via ordinary
`world_.createEntity()`/`setLocalTransform()`/`setParent()`/`setCamera()`/
`setRenderable()`/`setActiveCamera()` calls — a failure here (only
plausible from a genuine implementation bug) is treated identically to
every other `initializeSteps()` failure, returning a new
`RuntimeInitError::SceneConstructionFailed`.

**`runFrame()`'s existing Step 7 (camera write + `DrawItem` build) is
replaced, not extended, by:**

1. `world_.updateTransforms()`.
2. `world_.activeCamera()` — `std::nullopt` is
   `SceneExtractionError::NoActiveCamera`, treated as **unrecoverable**
   (`markFailed()`; return) — this Plan's own validation scene always sets
   one at construction, so reaching this path indicates a genuine
   construction bug.
3. `world_.getWorldMatrix(*activeCamera)` (guaranteed `Ok`) →
   `extractCameraMatrices(...)` (D7). Any `SceneExtractionError` here is
   likewise **unrecoverable** — this Plan's own fixed camera
   Transform/`Camera` values (D9) are chosen to never be degenerate.
4. Write `view`/`projection` into `cameraBuffer_->mappedData()`.
5. Build `std::vector<DrawItem> drawItems` (a per-frame local): for each
   `EntityId` in `world_.renderableEntities()` (ascending-slot-index
   order): `world_.getRenderable(id)` →
   `resolveMeshAsset(renderable.meshAsset, knownMinimalCubeAssetId_)`; on
   `Err(UnresolvedMeshAsset)`, log and **skip this one entity** —
   **recoverable, per-entity**, deliberately different from step 2/3's
   camera failures. On success, `world_.getWorldMatrix(id)` (guaranteed
   `Ok`) and append `DrawItem{&*mesh_, &*material_, worldMatrix}`.
6. `device_->createCommandList()`, `renderer_.drawFrame(...)`,
   `device_->submit(...)`, `presentation_->present(...)` — **all four
   calls unchanged in shape**; `drawFrame()` already accepts
   `std::span<const DrawItem>`.

**If `drawItems` is empty** (unreachable for this Plan's own scene, but
not structurally impossible for a future one): `Renderer::drawFrame()` is
still called with an empty span — `renderer.cpp`'s own `for` loop over
zero items is a legal no-op.

### D9. The validation scene — exact entities, values, and why the camera is static

**Six entities**, right-handed Y-up, world units at roughly the same 1–10
scale every existing composition root uses (the cube itself is 1×1×1):

| Entity | Parent | Local position | Local rotation (rad, x/y/z) | `Renderable` | `Camera` |
|---|---|---|---|---|---|
| A | root | (−2.5, 0, 0) | (0, 0, 0) | `minimal_cube` | — |
| B | root | (−1.0, 0, 0) | (0, 0.5236 [30°], 0) | `minimal_cube` | — |
| C | root | (1.0, −0.5, 0) | (0, 0, 0) | `minimal_cube` | — |
| D | **C** | (0, 1.3, 0) | (0, 0.7854 [45°], 0) | `minimal_cube` | — |
| E | root | (2.5, 0, 0) | (0.2618 [15°], 0.3491 [20°], 0) | `minimal_cube` | — |
| F | root | (0, 2.2, 7.0) | (−0.3054 [−17.5°], 0, 0) | — | `{60° fovY, 0.1, 100.0}`, active |

D's world position ends up at `(1.0, 0.8, 0)` (C has no rotation of its
own) — a visible, non-overlapping child offset from its parent, exercising
`setParent()`/hierarchy composition. F's own pitch is chosen so its
forward direction points from `(0, 2.2, 7.0)` back toward the cube row's
approximate centroid near the world origin — a reasoned, not
hand-verified-pixel-perfect, framing; visual fine-tuning of these exact
numbers during Implementation, before Step 8's golden capture, is
expected and is not an architectural change.

**No per-frame mutation of any entity's `Transform`** — Spec 0014's
Non-Goals exclude time-driven mutation from `World` itself; the scene is
built once, in `initializeSteps()`, and never touched again by `runFrame()`
(`updateTransforms()` still runs every frame, recomputing the same,
unchanged world matrices). "Moving/rotating a parent visibly moves its
child too" is satisfied by D/C's own static, correctly-composed geometry
— the rigorous proof is `tests/world/`'s own hand-computed-matrix unit
test (V9), not runtime animation. **Camera is static**, matching Spec
0013's own D5 precedent exactly, for the identical reason: a fixed camera
keeps the manual by-eye comparison against the new golden (D10) meaningful
frame-to-frame.

### D10. New headless image-regression fixture and golden — CMake wiring, ADR-0042 compliance

**New fixture, sibling to `MinimalCubeFixture`, same target
(`atlantis_image_regression_fixture`), same "duplicated, not shared"
precedent:** `tests/image_regression/fixture/world_scene_fixture.h`/`.cpp`
— a `WorldSceneFixture` struct shaped exactly like `MinimalCubeFixture`
**plus** `atlantis::world::World world` and
`atlantis::asset_system::AssetId knownMinimalCubeAssetId`.
`setUpWorldSceneFixture(artifactPath, metadataPath)` mirrors
`setUpMinimalCubeFixtureFromAsset()` exactly for every GPU-resource field
(one `Mesh`, one `Material`) and additionally builds D9's own six-entity
scene via the same `World` public API calls Runtime uses (duplicated
construction code, not shared). `renderOneWorldSceneFrame(fixture)`
mirrors `renderOneFrame()` through the acquire/copy/submit/`waitIdle()`
sequence, with its own camera-matrix-write and `DrawItem`-building steps
replaced by the same logic D8 describes, duplicated independently — this
fixture links `Atlantis::World` newly but reuses its own
already-duplicated `lookAt()`/`perspective()`/`identityMatrix()` helpers,
duplicated a *third* time into `world_scene_fixture.cpp`'s own anonymous
namespace.

**New GPU-required test case, same executable
(`atlantis_image_regression_gpu_tests`), new source file** —
`tests/image_regression/world_scene_gpu_tests.cpp`, comparing the captured
multi-entity frame against a **new** golden at
`tests/image_regression/goldens/world_scene/world_scene_512x512_rgba8unorm.png`
using the existing, unmodified `compareBuffers()`, requiring the same
**zero**-channel-difference bar the existing `minimal_cube` golden test
already requires. **The existing `minimal_cube` golden and its own test
case are untouched.**

**A new, second, standalone golden-generator executable — not a change to
the existing one — matching this Plan's own "duplicated, not shared"
precedent, for a concrete, disclosed reason: zero regression risk to the
already-working `minimal_cube` golden path.**
`tests/image_regression/golden_generator/world_scene_main.cpp`, new target
`atlantis_image_regression_world_scene_golden_generator`, reusing the
identical `Atlantis::ImageRegressionSupport`-provided git/provenance/PNG
steps (1–4, 6–9 of the existing tool's nine steps) with only step 5
("render one frame") replaced. **Never CTest-registered.**

**ADR-0042 "Initial baseline bootstrap" compliance — the exact procedure
the Implementation PR must follow and record:**

1. **Applicability (constraint 1):** confirmed satisfiable by construction
   — `tests/image_regression/goldens/world_scene/` does not exist on
   `main` today; this category is the **correct** one — not "Approved
   rebaseline," which requires old-vs-new diff evidence this first capture
   cannot produce.
2. **Source revision (constraint 2), commit ordering (constraint 4):**
   unchanged from the general rule — the code implementing this Plan is
   committed **first**, against a clean working tree; the golden PNG +
   sidecar are captured against that already-existing commit and added
   via a **separate, subsequent commit**.
3. **Full provenance (constraint 3):** the new sidecar carries the exact
   same field set the existing `minimal_cube` sidecar already does — the
   existing `serializeGoldenProvenance()` is reused unmodified; no new
   sidecar schema.
4. **Substitute evidence (constraint 5), recorded in the Implementation
   PR:** (a) a screenshot or attached PNG of the captured frame,
   human-reviewed to confirm a correctly-rendered, non-degenerate scene
   (five distinct, correctly-shaded, depth-ordered cubes, one visibly
   offset from its own parent, framed by the camera); (b) the real
   capture-compare cycle run immediately after capture, confirming
   **zero** channel difference against the golden it just wrote; (c) a
   real run on real Vulkan-capable GPU hardware with Validation Layers
   grepped clean; (d) citing ADR-0042's own already-recorded empirical
   calibration as the basis for requiring zero difference here too — this
   Plan does not re-run that calibration.
5. **No relaxation of any other Decision-section rule (constraint 6):**
   the golden validity check, comparison algorithm, provenance-mismatch
   diagnostic, and golden-regeneration-tool boundary all apply to this new
   golden exactly as they already apply to `minimal_cube`'s.

### D11. `WorldError`'s five enumerators are exhaustive for this Plan's own scope

Confirmed by construction: every setter/getter's own only failure modes
are "handle belongs to a different, live `World`" (`WrongWorld`, checked
first by `validate()`, D3), "handle stale or out-of-range"
(`InvalidEntity`), (for `setParent()`) "would create a cycle," (for
`setActiveCamera()`) "no `Camera` component," or (for `getRenderable()`)
"no `Renderable` component" — no sixth condition exists anywhere in D3–D5's
own algorithms. Every `WorldError`-producing `switch`/mapping in D3–D5
must handle all five enumerators exhaustively (verified by V28), never a
`default` case masking a missing one.

## Milestones / Task Breakdown

Each step leaves the repository configuring, building, and (from Step 1
onward) its own tests passing.

**Atomicity policy (revised, Plan Review Round 2):** a step (or part of a
step) is marked **atomic** only where splitting it would actually break
`cmake` configure/build, or violate a stated provenance rule (ADR-0042) —
not merely because grouping several related pieces of work into one step
was convenient to describe. Implementation may land the remainder of any
non-atomic step's own content as one commit or split it further, at its
own discretion, as long as every individual commit still configures and
builds. Step 8 remains a **mandatory separate commit** from Step 7 — a
provenance rule (must not be merged backward), a different kind of
constraint from atomicity (must not be split apart internally).

### Step 1 — `Atlantis::World` module skeleton, value types, slot map, entity lifecycle

The **only** genuine mechanical minimum: `src/world/CMakeLists.txt`'s own
`add_library(atlantis_world STATIC ...)` call and the root
`CMakeLists.txt`'s `add_subdirectory(src/world)` edit must land together
with **at least one** non-empty `.cpp` in that same commit. Nothing else
in this step's content below is mechanically required to land in the same
commit; it is grouped here for a cohesive, reviewable unit. Implementation
may split entity lifecycle, cascading destroy, identity/`WrongWorld`
checking, and `World`'s own special-member-function shape into separate
commits within this step.

- `src/world/CMakeLists.txt` — `atlantis_world` + `Atlantis::World` alias,
  D1's dependency list.
- `.../world/{vec3,entity_id,world_error,transform,camera,renderable}.h`
  — D2, in full, including `entity_id.h`'s `World`/`WorldIdentity` forward
  declarations, `EntityId`'s three-field shape, and `world_error.h`'s new
  `WrongWorld` enumerator.
- `.../world/world.h` / `src/world/src/world.cpp` — `World`'s full public
  API (D4) and internal slot map (D3), including the private
  `WorldIdentity` definition, the out-of-line constructor/destructor, the
  `validate()` helper, and the moved-from `ATLANTIS_CHECK_MSG` guard on
  every public method except `updateTransforms()`/`getWorldMatrix()`
  (Step 2) and `renderableEntities()` (grouped into Step 3, though it
  compiles here regardless).
- `tests/world/CMakeLists.txt`,
  `tests/world/{entity_lifecycle_tests.cpp,hierarchy_tests.cpp,module_boundary_tests.cpp}`
  — V1–V4, V6 (partial), V16, V22 (D3's copy/move `static_assert`s), and
  the identity-token coverage: V23–V26. `module_boundary_tests.cpp`
  created here (not later), mirroring `tests/asset_system/`'s own
  precedent of creating the boundary scan as soon as the module has any
  source, so it covers every later step's file automatically.
- Root `CMakeLists.txt` — `add_subdirectory(src/world)` and
  `add_subdirectory(tests/world)` (D1's ordering).

### Step 2 — Math contract and `updateTransforms()`

- `src/world/src/world.cpp` — the private matrix-math helpers (D6) and
  `updateTransforms()` (now fully iterative) and `getWorldMatrix()` (D6).
- `tests/world/{math_contract_tests.cpp,update_transforms_tests.cpp}` —
  V5, V7, V8, V9, V10.

### Step 3 — Camera and Renderable components, traversal, active-camera rule

- `src/world/src/world.cpp` — `setCamera()`/`removeCamera()`/`getCamera()`,
  `setActiveCamera()`/`clearActiveCamera()`/`activeCamera()`,
  `setRenderable()`/`removeRenderable()`/`getRenderable()`,
  `renderableEntities()` (D4).
- `tests/world/{camera_tests.cpp,renderable_tests.cpp,traversal_determinism_tests.cpp}`
  — V11, V12, V13, V14.

### Step 4 — Runtime: `scene_extraction.h`/`.cpp`, new `RuntimeInitError` enumerators

- `src/runtime/{include/atlantis/runtime/scene_extraction.h,src/scene_extraction.cpp}`
  — D7, in full (moved out of `runtime_application.cpp`'s own anonymous
  namespace: `lookAt()` → `lookAtMatrix()`, `perspective()` →
  `perspectiveMatrix()`, `identityMatrix()` stays, all now non-anonymous
  members of `atlantis::runtime` so `tests/runtime/` can call them
  directly).
- `src/runtime/{include/atlantis/runtime/init_error.h,src/init_error.cpp}`
  — two new `RuntimeInitError` enumerators, `AssetMetadataParseFailed` and
  `SceneConstructionFailed` (D8), added to the existing enum and mapping.
- `src/runtime/CMakeLists.txt` — `src/scene_extraction.cpp` added to
  `atlantis_runtime_host`'s existing source list; `Atlantis::World` added
  to that target's PUBLIC dependency list.
- `tests/runtime/CMakeLists.txt` — `scene_extraction_tests.cpp` added to
  the existing `atlantis_runtime_tests` source list — no new CMake
  target.
- `tests/runtime/{scene_extraction_tests.cpp,init_error_tests.cpp (extended)}`
  — V15 (camera-matrix extraction, all four cases: success, both
  degenerate inputs, the shear-robustness case reproducing V8's own
  counter-example directly), the `resolveMeshAsset()` match/no-match
  cases, and the two new `RuntimeInitError` enumerators' own
  `toString()`/exit-code mapping.

### Step 5 — `RuntimeApplication`: new members, scene construction, extraction-driven `runFrame()`

No genuine build/configure or provenance constraint applies — a
`RuntimeApplication` with D8's new members added but `runFrame()`'s Step 7
not yet replaced still compiles and links cleanly. Implementation may land
D8's new members, `initializeSteps()`'s new scene-construction step, and
`runFrame()`'s replaced Step 7 as one commit or split them further.

- `src/runtime/include/atlantis/runtime/runtime_application.h` — D8's new
  members.
- `src/runtime/src/runtime_application.cpp` — D6's metadata-for-`AssetId`
  read, D9's scene construction, D8's replaced `runFrame()` Step 7 (now
  calling `scene_extraction.h`'s functions instead of file-local helpers,
  which this step removes from this file's own anonymous namespace).
- No new automated test in this step, matching Plan 0013's own Step 3
  precedent: `atlantis_runtime_host` must still **compile** cleanly
  against `Atlantis::World`; this step's own new orchestration code is
  exercised for the first time by Step 6's GPU smoke test and verified in
  full by Step 8.

### Step 6 — Runtime GPU smoke test extension

- `tests/runtime/runtime_smoke_gpu_tests.cpp` — the existing `TEST_CASE`
  is unchanged in its own bounded-loop structure (D10 of Plan 0013); its
  assertions are extended to additionally confirm the acquired frame's
  `DrawItem` count reaching `Renderer::drawFrame()` is `5` on a successful
  run — the first `gpu`-labeled confirmation that this Plan's own
  multi-item span actually reaches `Renderer::drawFrame()` with more than
  one item.

### Step 7 — New headless fixture, golden generator, GPU test case

No genuine build/configure constraint applies: a `.cpp`/`.h` pair added
under `tests/image_regression/fixture/` but not yet referenced by
`CMakeLists.txt`'s own `add_library()` source list simply is not compiled.
Grouped here as one cohesive, reviewable unit; Implementation may split
the fixture code, its CMake wiring, the new GPU test case, and the new
golden-generator executable into separate commits if it prefers.

- `tests/image_regression/fixture/{world_scene_fixture.h,.cpp}` — D10.
- `tests/image_regression/fixture/CMakeLists.txt` — `Atlantis::World`
  added to `atlantis_image_regression_fixture`'s PUBLIC link list; the new
  `.cpp` added to its `add_library()` source list.
- `tests/image_regression/world_scene_gpu_tests.cpp` — D10's new
  `TEST_CASE`, comparing against the not-yet-existing golden (this test is
  expected to fail — `INVALID GOLDEN`, per ADR-0042's validity check —
  until Step 8 captures it; disclosed, not a defect in this step's
  commit).
- `tests/image_regression/CMakeLists.txt` — the new source file added to
  `atlantis_image_regression_gpu_tests`'s existing source list;
  `Atlantis::World` added to that target's link libraries.
- `tests/image_regression/golden_generator/world_scene_main.cpp` — D10.
- `tests/image_regression/golden_generator/CMakeLists.txt` — the new
  `add_executable()` call (D10), reusing the existing
  `Atlantis::ImageRegressionSupport`/`Atlantis::ImageRegressionFixture`
  link libraries, never CTest-registered.

### Step 8 — Golden capture (**must be its own separate, subsequent commit — a provenance rule, not an atomicity rule; never folded backward into Step 7's own commit(s), per D10's own ADR-0042 procedure**)

- Run `atlantis_image_regression_world_scene_golden_generator
  world_scene/world_scene_512x512_rgba8unorm` against a clean working
  tree, on real Vulkan-capable hardware.
- `tests/image_regression/goldens/world_scene/{world_scene_512x512_rgba8unorm.png,world_scene_512x512_rgba8unorm.sidecar.txt}`
  — the tool's own output, committed as-is.
- Re-run `atlantis_image_regression_world_scene_gpu_tests` (now passing,
  zero channel difference against the golden just captured) — D10's own
  constraint-5(b) self-consistency evidence.

### Step 9 — Full verification (Debug/Release, GPU-independent, GPU-required, Validation Layers, existing regression, manual)

- Clean Debug and Release configure + build.
- `ctest -LE gpu` and `ctest -L gpu`, both configurations — `-LE gpu`
  confirms V1–V16; `-L gpu` confirms V17 (`runtime_smoke_gpu_tests`, now
  requiring the extended `atlantis_runtime`) and V18
  (`world_scene_gpu_tests`, alongside the existing, unmodified
  `image_regression_gpu_tests`); together, on both configurations, V19.
- Vulkan Validation Layers grepped clean on every GPU-touching path, both
  configurations (part of V19).
- Manual windowed verification of the real `atlantis_runtime` executable:
  a visible window shows five distinct cube instances at their D9-specified
  relative positions (including D visibly offset from and attached to C),
  correctly shaded and depth-ordered, matching the newly-captured golden
  by eye; interactive resize, minimize/restore, and a normal close all
  behave exactly as Spec 0013's own already-verified bar requires — V20.
- `git diff --stat` confirms no file under `src/rhi/`, `src/renderer/`,
  `src/render_graph/`, `src/vulkan_backend/`, `src/platform/`,
  `src/shader_system/`, `src/asset_system/`, `shaders/`, or the existing
  `tests/image_regression/goldens/minimal_cube/` golden was modified —
  V21.

### Step 10 — Documentation and registry closeout

- `AGENTS.md` — the Module boundaries section gains an `Atlantis::World`
  paragraph; the Runtime paragraph's own dependency list gains
  `Atlantis::World`.
- `docs/architecture/module_boundaries.md` — a new `## Atlantis World`
  section, matching the existing per-module format; the `## Atlantis
  Runtime` section's own `Depends on` line gains `Atlantis::World`.
- `docs/project-blueprint.md` — a new Milestone entry for World / Scene
  Foundation.
- `src/README.md` — add a `world/` entry.
- `specs/README.md` — Spec 0014's own Implementation column updated once
  the Implementation PR is opened (matching Spec 0012/0013's own
  established two-stage "OPEN, not yet merged" → "Implemented and merged"
  convention). This Plan's own Plan-column edits are separate from, and
  precede, that future Implementation-column edit.

## Files / Modules Touched (expected)

**New — World module:** `src/world/CMakeLists.txt`;
`src/world/include/atlantis/world/{vec3,entity_id,world_error,transform,camera,renderable,world}.h`;
`src/world/src/world.cpp`.

**New — Runtime extension:**
`src/runtime/include/atlantis/runtime/scene_extraction.h`,
`src/runtime/src/scene_extraction.cpp`.

**New — tests:**
`tests/world/{CMakeLists.txt,entity_lifecycle_tests.cpp,hierarchy_tests.cpp,math_contract_tests.cpp,update_transforms_tests.cpp,camera_tests.cpp,renderable_tests.cpp,traversal_determinism_tests.cpp,module_boundary_tests.cpp}`;
`tests/runtime/scene_extraction_tests.cpp`;
`tests/image_regression/fixture/world_scene_fixture.{h,cpp}`;
`tests/image_regression/world_scene_gpu_tests.cpp`;
`tests/image_regression/golden_generator/world_scene_main.cpp`.

**New — golden:**
`tests/image_regression/goldens/world_scene/{world_scene_512x512_rgba8unorm.png,world_scene_512x512_rgba8unorm.sidecar.txt}`
(Step 8's own separate, subsequent commit).

**Modified:** `CMakeLists.txt` (root) — two `add_subdirectory()` lines;
`src/runtime/CMakeLists.txt` — `Atlantis::World` added, `scene_extraction.cpp`
added (Step 4); `src/runtime/include/atlantis/runtime/runtime_application.h`
— D8's new members; `src/runtime/src/runtime_application.cpp` — D6/D8/D9's
new initialization steps and replaced `runFrame()` Step 7; the file-local
`lookAt()`/`perspective()` helpers removed; `src/runtime/include/atlantis/runtime/init_error.h`,
`src/runtime/src/init_error.cpp` — two new enumerators;
`tests/runtime/CMakeLists.txt` — `scene_extraction_tests.cpp` added (Step
4); `tests/runtime/runtime_smoke_gpu_tests.cpp` — extended assertion (Step
6); `tests/runtime/init_error_tests.cpp` — extended for the two new
enumerators; `tests/image_regression/fixture/CMakeLists.txt` — additive;
`tests/image_regression/CMakeLists.txt` — additive;
`tests/image_regression/golden_generator/CMakeLists.txt` — additive;
`AGENTS.md`, `docs/architecture/module_boundaries.md`,
`docs/project-blueprint.md`, `src/README.md`, `specs/README.md` (Step 10);
`specs/0014-world-scene-foundation.md` — `Related Plan(s)` field (this
Plan-drafting round).

**Explicitly not touched:** `src/rhi/`, `src/renderer/`,
`src/render_graph/`, `src/vulkan_backend/`, `src/platform/`,
`src/shader_system/`, `src/asset_system/`, `src/tools/`, `assets/`,
`shaders/`, `examples/`, `tests/asset_system/`, `tests/shader_system/`,
`tests/rhi/`, `tests/vulkan_backend/`, `tests/render_graph/`,
`tests/renderer/`, `tests/platform/`, `tests/core/`, `tests/tools/`,
`cmake/`, every existing example/demo, the existing `minimal_cube` golden
and its own test case, and every existing `Accepted` ADR.

## Sequencing & Dependencies

```
Step 1 (World skeleton, value types, slot map, entity lifecycle)
  └─> Step 2 (math contract, updateTransforms())
        └─> Step 3 (Camera/Renderable, traversal, active-camera rule)
              ├─> Step 4 (Runtime: scene_extraction.h/.cpp, new RuntimeInitError enumerators)
              │     └─> Step 5 (RuntimeApplication: members, scene construction, runFrame())
              │           └─> Step 6 (Runtime GPU smoke test extension)
              └─> Step 7 (new headless fixture, golden generator, GPU test case)
                    └─> Step 8 (golden capture, separate commit)
                          └─> Step 9 (full verification)
                                └─> Step 10 (documentation/registry closeout)
```

- Step 4 needs Step 3 because `scene_extraction_tests.cpp`'s own
  `resolveMeshAsset()` cases use values consistent with what `Renderable`
  (Step 1) already carries, and because Step 4's own camera-matrix tests
  reuse D9's counter-example transform values.
- Step 5 needs Step 4 for `scene_extraction.h`'s own functions to exist.
- Step 7 needs Step 3 (a complete `World` public API) but is otherwise
  independent of Steps 4–6 — the two branches may be implemented and
  reviewed in either order, or in parallel.
- Step 8 needs Step 7's own executable to exist, and must be a **separate
  commit** from Step 7 even though it depends on Step 7 having landed.
- Step 9 needs both Step 6 and Step 8.

## Verification Checklist

| # | Verification | Where | Kind |
|---|---|---|---|
| V1 | `createEntity()` always succeeds, returns a valid handle; a fresh `World` has no live entities. | `entity_lifecycle_tests.cpp` | GPU-independent |
| V2 | `destroyEntity()`: invalidates the target and, recursively, every transitive descendant, in one call; every subsequent operation against any of those handles returns `Err(InvalidEntity)`; an unrelated sibling/ancestor is untouched. An out-of-range `index` is rejected the same way. Same-`World`-instance behavior only — cross-instance behavior is V23–V25. | `entity_lifecycle_tests.cpp` | GPU-independent |
| V3 | Slot reuse: destroying and recreating produces a **different** `generation()` at the same `index()`; the old `EntityId` is correctly `Err(InvalidEntity)`; the free list's own LIFO order is directly observed via the public `index()` accessor (destroy A then B; the next two `createEntity()` calls reuse B's `index()` first, then A's). | `entity_lifecycle_tests.cpp` | GPU-independent |
| V4 | **Generation retirement, at the real boundary:** a slot's generation is set to `max() - 1`, `destroyEntity()`d, and confirmed (a) its generation is now the tombstone value; (b) a following `createEntity()`, or a sequence exhausting every other free slot, never reuses that index; (c) an `EntityId` at that index with its old, pre-retirement generation still correctly returns `Err(InvalidEntity)`. | `entity_lifecycle_tests.cpp` | GPU-independent |
| V5 | `setParent()`: succeeds for a valid, non-cycle-forming request; rejects a direct self-parent, a two-hop cycle, and a four-hop transitive cycle, each with `Err(WouldCreateCycle)`, leaving the hierarchy **completely unchanged**; a stale `child`/`parent` handle is `Err(InvalidEntity)`, checked before any cycle walk. | `hierarchy_tests.cpp` | GPU-independent |
| V6 | Every mutating call's own `Err` path leaves every observable `World` state — parent links, local transforms, component presence, `isValid()` for every other entity — byte-identical to immediately before the call. | `hierarchy_tests.cpp`, `entity_lifecycle_tests.cpp` | GPU-independent |
| V7 | `updateTransforms()`/`getWorldMatrix()`: a multi-level chain (root → child → grandchild), each with a distinct `Transform`, produces world matrices matching an independently hand-computed expected result to floating-point tolerance. A root entity's world matrix equals its own local matrix. | `math_contract_tests.cpp` | GPU-independent |
| V8 | **Shear under a scaled hierarchy:** a parent with `localScale = (2,1,1)` composed with a child rotated 45° about `Z` (ADR-0050's counter-example, reproduced exactly) produces a world matrix whose own linear-part columns 0/1 have a non-zero dot product, matching the hand-computed `−1.5` value ADR-0050 records. | `math_contract_tests.cpp` | GPU-independent |
| V9 | Moving/rotating a parent, then `updateTransforms()`, changes a child's own `getWorldMatrix()` result in exactly the way composing the new parent matrix with the child's unchanged local matrix predicts. | `update_transforms_tests.cpp` | GPU-independent |
| V10 | `setParent()` preserves the child's own `getLocalTransform()` byte-for-byte across a reparent; its `getWorldMatrix()` (after `updateTransforms()`) changes when, and only when, the old and new parent's own world matrices actually differ. | `update_transforms_tests.cpp` | GPU-independent |
| V11 | `Camera`: `setCamera()`/`getCamera()`/`removeCamera()` round-trip and correctly report `Err(InvalidEntity)` on a stale handle; `setActiveCamera()` fails `Err(NoCameraComponent)` against an entity with no `Camera`; destroying the active camera entity (directly, or transitively via cascading destroy of an ancestor) clears `activeCamera()` to `std::nullopt` automatically; a fresh `World`'s own `activeCamera()` starts `std::nullopt`. | `camera_tests.cpp` | GPU-independent |
| V12 | `Renderable`: `setRenderable()`/`getRenderable()`/`removeRenderable()` round-trip, by value. | `renderable_tests.cpp` | GPU-independent |
| V13 | Every public getter returns a plain value, never a reference/pointer — confirmed via `static_assert` on each method's own return type. | `entity_lifecycle_tests.cpp` (or a dedicated file) | GPU-independent (compile-time) |
| V14 | **Deterministic enumeration:** a fixed sequence of `createEntity()`/`destroyEntity()` calls exercising the LIFO free list produces the exact same `renderableEntities()` ascending-slot-index ordering across repeated, independent runs. | `traversal_determinism_tests.cpp` | GPU-independent |
| V15 | `extractCameraMatrices()`: a well-formed input (including the V8 shear-producing configuration) produces an orthonormal `view` basis and a correct `projection`; a near-zero-length forward returns `Err(DegenerateCameraForward)`; a forward parallel to `(0,1,0)` returns `Err(DegenerateCameraBasis)`; a negatively-scaled ancestor's world matrix still produces a proper (determinant `+1`) `view` basis. `resolveMeshAsset()`: matching and non-matching `AssetId` pairs each return the correct `Ok`/`Err(UnresolvedMeshAsset)`. | `scene_extraction_tests.cpp` | GPU-independent |
| V16 | No source under `src/world/` includes `atlantis/rhi/`, `atlantis/renderer/`, `atlantis/render_graph/`, `atlantis/shader_system/`, `atlantis/platform/`, `atlantis/vulkan_backend/`, `atlantis/runtime/`, or any `vulkan` header. | `module_boundary_tests.cpp` | GPU-independent |
| V17 | Real GPU, windowed: `atlantis_runtime_gpu_tests`'s extended assertion confirms exactly 5 `DrawItem`s reach `Renderer::drawFrame()` on a successful frame; Vulkan Validation Layers report zero warnings/errors for the full multi-item span. | `runtime_smoke_gpu_tests.cpp` | `gpu`-labeled |
| V18 | Real GPU, headless: the new `world_scene` golden compare achieves **zero** channel difference; the `minimal_cube` golden's own existing test still passes, unmodified, proving the new fixture's own construction did not disturb the existing one. | `world_scene_gpu_tests.cpp`; `image_regression_gpu_tests.cpp` (existing, re-run) | `gpu`-labeled |
| V19 | Debug **and** Release: clean configure + build; `ctest -LE gpu` and `ctest -L gpu` both green on both configurations; Vulkan Validation Layers grepped clean on every GPU-touching path. | Both configurations | Manual, recorded |
| V20 | Manual windowed: the real `atlantis_runtime` executable shows five distinct, correctly-shaded, depth-ordered cubes at their D9 positions (D visibly offset from C), matching the new golden by eye; interactive resize/minimize/restore/close all behave per Spec 0013's own already-established bar. **This is visual confirmation only — it is not, and does not substitute for, the mathematical proof of Camera scale/degenerate-input correctness, which V15 alone provides.** | Manual | Manual |
| V21 | `git diff --stat` confirms no file under `src/rhi/`, `src/renderer/`, `src/render_graph/`, `src/vulkan_backend/`, `src/platform/`, `src/shader_system/`, `src/asset_system/`, `shaders/`, or `tests/image_regression/goldens/minimal_cube/` was modified, and no existing example/demo/CMake target/test was removed or renamed. | Manual, recorded in the PR | Manual |
| V22 | `World` is move-constructible, and **not** copy-constructible, copy-assignable, or move-assignable — confirmed via `static_assert` on each trait. | `tests/world/` (dedicated or added to `entity_lifecycle_tests.cpp`) | GPU-independent (compile-time) |
| V23 | **Two simultaneously live `World` instances, first-entity collision:** construct `World a; World b;`, call `a.createEntity()` and `b.createEntity()` (both correctly `{index=0, generation=0}`, differing only in their private identity); confirm `b`'s every `EntityId`-accepting API rejects `a`'s handle with `Err(WrongWorld)` (not `InvalidEntity`, not coincidental `Ok`), and symmetrically. | `entity_lifecycle_tests.cpp` | GPU-independent |
| V24 | **`WrongWorld` reachable from every `EntityId`-accepting entry point** — `setParent()`/`getParent()`, `setLocalTransform()`/`getLocalTransform()`, `getWorldMatrix()`, `setCamera()`/`removeCamera()`/`getCamera()`, `setActiveCamera()`, `setRenderable()`/`removeRenderable()`/`getRenderable()` each correctly return `Err(WrongWorld)` when passed a handle from a different, live `World` instance, checked **before** any index/generation-dependent behavior. `kInvalidEntityId` itself is confirmed to still report `Err(InvalidEntity)`, never `WrongWorld`, against every one of these. | `entity_lifecycle_tests.cpp`, `hierarchy_tests.cpp` | GPU-independent |
| V25 | **`EntityId` validity survives `World` move-construction:** create several entities (including a parent/child pair), capture their `EntityId`s, move-construct a new `World` from it, and confirm every previously issued `EntityId` is still `Ok`/`isValid()` against the moved-to instance — same index, generation, identity token address, hierarchy/component data intact. | `entity_lifecycle_tests.cpp` | GPU-independent |
| V26 | **Moved-from `World`: any call other than the destructor or move-construction-from is a checked programmer error.** After a move, install a recording failure handler (`atlantis::assertions::setFailureHandler()`), call `original.createEntity()` (and separately an arbitrary `EntityId`-accepting method, `updateTransforms()`, `clearActiveCamera()`/`activeCamera()`), confirm the handler recorded exactly one failure each time. | `entity_lifecycle_tests.cpp` | GPU-independent |
| V27 | **`EntityId`'s full encapsulation, all three fields (2026-08-23 correction):** (a) `std::is_trivially_copyable_v<EntityId>`; (b) no external code can compile a direct mutation of `index_`/`generation_`/`worldIdentity_`; (c) the defaulted `operator==` distinguishes two handles differing in **any single one** of the three fields; (d) `kInvalidEntityId` still reports `Err(InvalidEntity)` (never `WrongWorld`) against every `World`-issued handle, and every `World::createEntity()` handle still round-trips through every accessor/API; (e) no test or production code asserts or depends on a fixed `sizeof(EntityId)`. | `entity_lifecycle_tests.cpp` | GPU-independent (mix of runtime and compile-time) |
| V28 | **`WorldError::NoRenderableComponent`, distinct from `NoCameraComponent` (2026-08-23 Human Review Correction):** (a) `getRenderable()` against a valid entity with no `Renderable` returns `Err(NoRenderableComponent)`, never `Err(NoCameraComponent)`; (b) `setActiveCamera()` against a valid entity with no `Camera` still returns `Err(NoCameraComponent)`, never `Err(NoRenderableComponent)` — both checked on the same entity, to directly prove the two errors are distinct; (c) for `getRenderable()`, an invalid, wrong-`World`, or stale-generation handle still returns `Err(InvalidEntity)`/`Err(WrongWorld)` and never reaches the component-absence check; (d) every `WorldError`-producing `switch`/mapping in the implementation is confirmed exhaustive over all five enumerators — no `default:` case. | `renderable_tests.cpp`, `camera_tests.cpp` | GPU-independent |

## Traceability — Spec / ADR → Plan

| Source requirement | Where satisfied |
|---|---|
| Spec 0014 — New top-level `Atlantis::World`, Core+AssetSystem(narrow) only | D1; V16 |
| Spec 0014 — `EntityId` index+64-bit-generation, by-value access, atomic mutation | D2, D3, D4; V1, V6, V13 |
| Spec 0014 — Overflow formally closed via slot retirement | D3; V4 |
| Spec 0014 — Stale-handle `Result` classification | D4; V2 |
| `EntityId` gains three `private` fields; `World` owns a heap-allocated, address-stable identity token — ADR-0049/Spec 0014 Accepted Amendments | D2, D3, D4, D5; V22–V27 |
| Moved-from `World`: destructible/move-constructible-from only, every other call a checked programmer error | D3, D4; V26 |
| `World`'s own copy/move semantics, now load-bearing for identity | D3; V22, V25 |
| `WorldError::WrongWorld` — new fourth enumerator, checked before slot/generation at every entry point | D2, D3, D4, D5, D11; V23, V24 |
| `WorldError::NoRenderableComponent` — new fifth enumerator (Human Review Correction, 2026-08-23) | D2, D4, D11; V28 |
| Spec 0014 — Deterministic slot reuse (LIFO) and enumeration (ascending index) | D3, D4; V3, V14 |
| Spec 0014 — Fixed-type `Transform`/`Camera`/`Renderable` component storage | D2, D4; V11, V12 |
| Spec 0014 — `setParent()` cycle prevention, local-vs-world preservation | D5; V5, V10 |
| Spec 0014 — Cascading destroy, active-camera auto-clear | D3; V2, V11 |
| Spec 0014 — Explicit `updateTransforms()`, no automatic propagation | D6; V7, V9 |
| Spec 0014 — Full math contract (layout, order, handedness, TRS, Euler order) | D6; V7 |
| Spec 0014 — Composed-hierarchy shear, disclosed and handled correctly downstream | D6, D7; V8, V15 |
| Spec 0014 — Camera `eye`/`forward`-only extraction, degenerate-input errors | D7; V15 |
| Spec 0014 — Runtime-private `AssetId`→`Mesh`/`Material` resolution, no global database | D6, D7, D8; V15 |
| Spec 0014 — World→Renderer adapter stays Runtime's own responsibility | D8; V17 |
| Spec 0014 — No RHI/Renderer/RenderGraph/VulkanBackend/Platform dependency from `src/world/` | D1; V16 |
| Spec 0014 — No existing public rendering API changed | Files/Modules Touched's "Explicitly not touched" list; V21 |
| Spec 0014 — Multi-entity Runtime validation scene, windowed smoke/manual verification | D9; V17, V19, V20 |
| Spec 0014 — Headless golden, "Initial baseline bootstrap" category | D10; V18 |
| ADR-0048 — Module boundary, dependency direction, no general Math module | D1; V16 |
| ADR-0049 — `EntityId` shape, retirement rule, `Result` classification, determinism, atomicity | D2, D3, D4, D5; V1–V6, V14 |
| ADR-0050 — Math contract, hierarchy, cycle prevention, cascading destroy, update model | D5, D6; V5, V7, V8, V9, V10 |
| ADR-0051 — Extraction/adapter boundary, camera construction, asset resolution privacy | D7, D8, D10; V15, V17, V18 |

## Rollback Plan

Every step through Step 6 is additive to Runtime and wholly new for
`src/world/`/`tests/world/`. Reverting the Implementation PR (Steps
1–7, 9–10; Step 8's golden lands in its own commit, see below) removes
`src/world/`, `tests/world/`, `src/runtime/include/atlantis/runtime/scene_extraction.h`,
`src/runtime/src/scene_extraction.cpp`,
`tests/image_regression/fixture/world_scene_fixture.{h,cpp}`,
`tests/image_regression/world_scene_gpu_tests.cpp`,
`tests/image_regression/golden_generator/world_scene_main.cpp`, and
reverts the additive-only edits to `runtime_application.{h,cpp}`,
`init_error.{h,cpp}`, and every touched `CMakeLists.txt`. Because no
existing module's public API is modified and the existing `minimal_cube`
golden is untouched, revert restores the exact pre-Plan build and test
behaviour with no migration step. Step 8's own golden-capture commit
reverts independently and trivially (delete the two new golden files);
its own preceding commit (Step 7) remains fully buildable without it —
`world_scene_gpu_tests.cpp` simply reports `INVALID GOLDEN` again, not a
build failure.

## Deviations, objections, and open mechanical details

**No `Accepted` decision in Spec 0014 or ADR-0048–0051 was found to be
wrong or unimplementable while drafting this Plan.** One genuine gap the
`Accepted` text left *unaddressed* (not wrong, merely silent) was
escalated to Human Review rather than silently resolved, and is now
formally settled — see "RESOLVED" below. One genuine implementability
question the Spec/ADRs left open — how Runtime obtains `minimal_cube`'s
real `AssetId` without a new Asset System dependency edge — is resolved in
D6 using two already-`Accepted`, already-public Asset System functions
neither the Spec nor the ADRs named explicitly.

**Three duplication decisions, each disclosed with its own concrete
reason, not silently accumulated:** (1) `scene_extraction.h`'s own
camera-math functions are new, factored-out **Runtime-private** code, not
shared with the new image-regression fixture, which duplicates the same
logic independently — matching ADR-0051's own explicit rejection of a
shared "Extraction module," and this codebase's long-standing
"duplicated, not shared" precedent (already duplicated three times over
across `examples/minimal_renderer_demo`, `minimal_cube_fixture.cpp`, and
`src/runtime/`, before this Plan's fourth copy). (2) The new golden
generator is a **second, standalone** executable, not a change to the
existing one — a deliberate, small (~60 line) boilerplate duplication
cost, accepted to keep zero regression risk to the already-verified
`minimal_cube` golden path. (3) D9's validation-scene construction code is
written once for Runtime and once, independently, for the headless
fixture — not factored into a shared "build the scene" function.

**Two genuinely open mechanical details, appropriately left to
Implementation, neither architectural:** (1) the exact numeric
camera-framing values in D9 are a reasoned starting point — if the actual
rendered frame does not comfortably frame all five cubes once first
rendered, Implementation may adjust entity F's own position/rotation
before Step 8's golden capture; a numeric tuning pass within an
already-fixed scene *structure*, not a new architectural decision. (2)
The test-only friend-access mechanism V4 needs (D3's own
generation-retirement boundary test) is left as "a documented, narrowly-
scoped `friend` declaration naming the one test translation unit that
needs it" — the exact class/function name is an Implementation-time
detail.

**RESOLVED (Round 4): cross-`World`-instance `EntityId` use — a real gap
in the `Accepted` contract found during this Plan's own review, escalated
to Human Review rather than resolved unilaterally, and now fully resolved
by formal Human Review Approval.** Round 1 found the gap and initially
reasoned it away as "not reachable by any code this Plan writes" —
rejected in Round 2 as insufficient justification for a public module's
own correctness, since two freshly constructed `World` instances both
hand out `{index=0, generation=0}` for their own first entity, a
guaranteed cross-instance collision. Round 2 escalated the question with
two options (document as UB, or add per-instance identity). Human Review
responded: Option A (UB) rejected; Option B directed, specifically as a
stable, heap-allocated, address-stable per-`World` identity token —
explicitly not a global instance counter, not `shared_ptr`/`weak_ptr`-based
identity, and not a restriction to a single `World` instance per process.
Round 3 applied that direction throughout D2–D5/D11 and V22–V25. Round 4
applied two further refinements Human Review added when formally
approving the design — the identity field made `private`, and an explicit
moved-from-`World` contract (V26) — and recorded formal Human Review
**Amendment Approval** on both ADR-0049 and Spec 0014. **Both amendments
are now formally `Accepted`** — this Plan's own Human Review Approval is
no longer blocked. Implementation itself still waits on
[PR #67](https://github.com/slmao/Atlantis/pull/67) being merged.

**RESOLVED (Round 6, 2026-08-23): `getRenderable()` reusing
`NoCameraComponent` — a Human Review Correction, not a Plan defect found
during drafting.** This gap was disclosed by Implementation itself on
[PR #68](https://github.com/slmao/Atlantis/pull/68) after this Plan's own
Human Review Approval. Human Review reviewed and **rejected** the reuse
(2026-08-23), directing a fifth, independent enumerator instead. This
governance correction does not by itself change PR #68's Implementation
code — the actual `NoRenderableComponent` enumerator and corrected
`getRenderable()` body land in a separate, subsequent Implementation-fix
change. **This correction also does not touch, waive, or reinterpret
V20** — V20 requires an actual human, using a real graphical session, to
personally observe the windowed `atlantis_runtime` executable's five-cube
scene, resize/minimize/restore/close, and clean Vulkan Validation Layers
output — a programmatic Win32 message-injection pass is useful automation
but is **not** a substitute for, and must never be recorded as satisfying,
V20's own human-observation requirement.

**Post-Merge Status Update (2026-08-23):**
[PR #68](https://github.com/slmao/Atlantis/pull/68) (Implementation),
[PR #69](https://github.com/slmao/Atlantis/pull/69) (this governance
correction), and [PR #70](https://github.com/slmao/Atlantis/pull/70) (the
`WorldError::NoRenderableComponent` code fix, V28, and a broadened final
code review) are all merged. V20 was subsequently satisfied by an actual
human, using a real graphical session, personally observing both the
Debug and Release `atlantis_runtime` executables — five-cube scene
visible and correctly composed, interactive resize/minimize/restore/close
all correct, both processes exiting with code 0, Vulkan Validation Layers
zero VUID/Error/Warning in both runs — recorded as a PR comment on
[PR #70](https://github.com/slmao/Atlantis/pull/70/) dated 2026-08-23.
This does not alter the point above: the programmatic Win32 automation
itself never satisfied V20; a separate, subsequent human observation did.

**One disclosed, deliberately unmitigated edge case, distinct from the
generation-retirement risk ADR-0049 already closes:** `EntityId::index`
reaching its own `std::uint32_t` maximum (a `World` instance holding over
4 billion **simultaneously alive** slots) is not mitigated by this Plan,
unlike per-slot generation exhaustion. Reaching it would require memory
this Spec's own validation scale never approaches (each `Slot` is well
over 40 bytes; 4 billion of them exceeds 160 GB) — a fundamentally
different, memory-bounded impossibility, which is why ADR-0049 itself
never asked for an index-side mitigation.

## Definition of Done

See [docs/process/definition-of-done.md](../docs/process/definition-of-done.md).
Deltas specific to this plan:

- V1–V28 all executed and recorded, including V20. `ctest -LE gpu`:
  436/436 Debug, 435/435 Release; `ctest -L gpu`: 19/19 both
  configurations; Vulkan Validation Layers grepped clean throughout.
- **V20 specifically requires genuine human observation through a real
  graphical session** — programmatic Win32 message-injection automation
  is not, and was never recorded as, a substitute. **Satisfied
  2026-08-23**: an actual human personally observed both Debug and
  Release `atlantis_runtime` (five-cube scene, interactive
  resize/minimize/restore/close, clean exit, clean Validation Layers),
  recorded as a PR comment on
  [PR #70](https://github.com/slmao/Atlantis/pull/70) — see "Post-Merge
  Status Update" above.
- Both the ADR-0049 and Spec 0014 Accepted Amendments (stable `World`
  identity token) reached their formal Human Review Amendment Approval
  record — done, 2026-08-22 — before this Plan's own Human Review
  Approval; Implementation itself still waits on
  [PR #67](https://github.com/slmao/Atlantis/pull/67) being merged.
- The existing `minimal_cube` golden under
  `tests/image_regression/goldens/` is confirmed **unchanged** in the
  final diff; the new `world_scene` golden is captured per D10's own exact
  ADR-0042 "Initial baseline bootstrap" procedure, in its own separate
  commit (Step 8).
- `git diff --stat` confirms no file under `src/rhi/`, `src/renderer/`,
  `src/render_graph/`, `src/vulkan_backend/`, `src/platform/`, or
  `src/shader_system/` was modified.
- No new third-party dependency appears in `cmake/` or any
  `CMakeLists.txt`.
