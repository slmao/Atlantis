# Plan: World / Scene Foundation

- **Spec:** [specs/0014-world-scene-foundation.md](../specs/0014-world-scene-foundation.md) (`Approved`, Human Review Approval recorded 2026-08-22; carries its own "Proposed Amendment" section, 2026-08-22, not yet `Accepted` — see Independent Review Round 3 below)
- **Status:** In Review — **Human Review Approval is blocked**, not merely
  pending: Independent Review Round 2 escalated a cross-`World`-instance
  `EntityId` question to Human Review. Human Review has since given a
  concrete design direction (2026-08-22 — a stable, heap-allocated
  `World` identity token; see below), now fully reflected in this
  document's own D2–D5/D11 and Verification Checklist. **The amendment
  itself is still `Proposed`, not `Accepted`, on both
  [ADR-0049](../adr/0049-entity-identity-and-handle-invalidation.md) and
  [Spec 0014](../specs/0014-world-scene-foundation.md)** — a direction is
  not the same as a recorded Approval. This Plan cannot proceed to its
  own Human Review Approval, and Implementation must not begin, until
  both amendments are formally `Accepted` — see Deviations.
- **Author:** Drafted by Claude Code (AI agent) at explicit human
  direction, following AGENTS.md's Spec → Plan → Human Review →
  Implementation path. Not yet reviewed by a human — see Independent
  Review below for the self-review performed during drafting; Human
  Review Approval is not recorded on this document yet.
- **Independent Review (2026-08-22):** Self-review performed during
  drafting, against `main`'s actual, current source tree (not the Spec's
  own illustrative prose alone) for every file/target this Plan touches
  or extends:
  - `src/runtime/{CMakeLists.txt,include/atlantis/runtime/*.h,src/*.cpp,main.cpp}`
    — read in full. Confirmed `RuntimeApplication`'s exact current member
    layout, its six-step `initializeSteps()`, its ten-step `runFrame()`,
    its single-path `shutdown()`, and that `mesh_`/`material_` are
    already `std::optional<Mesh>`/`std::optional<Material>` (singular,
    not a container) — directly relevant to D6 below, since this Plan's
    own validation scene reuses the **same** `minimal_cube` `Mesh`/
    `Material` for every `Renderable` entity, so neither member needs to
    become a container.
  - `src/renderer/include/atlantis/renderer/{draw_item,mesh,material,renderer}.h`
    and `src/renderer/src/renderer.cpp` — re-confirmed `DrawItem`'s exact
    fields and `Renderer::drawFrame()`'s existing multi-item span
    iteration (`for (const DrawItem& item : drawItems)`) — unchanged
    since Spec 0014's own Independent Review; this Plan touches none of
    these files.
  - `src/asset_system/include/atlantis/asset_system/{asset_id,asset_metadata,load,static_mesh_asset_data}.h`
    — confirmed `StaticMeshAssetData` (returned by `loadStaticMeshAsset()`)
    carries **no** `AssetId` field, but `asset_metadata.h`'s own
    already-public `AssetMetadata` struct (returned by the also-already-
    public `parseAssetMetadata(std::string_view)`) **does** carry
    `assetId` — directly resolving how this Plan obtains `minimal_cube`'s
    real `AssetId` without touching Asset System's own public API or
    CMake surface at all (D6).
  - `src/asset_system/CMakeLists.txt` and `assets/CMakeLists.txt` — read
    in full. Confirmed `atlantis_add_static_mesh_asset()` exports exactly
    three `PARENT_SCOPE` variables (`ARTIFACT_PATH`, `METADATA_PATH`,
    `TARGET` — no logical-path or Asset-ID variable), and that
    `minimal_cube`'s own declared `SOURCE` is exactly
    `meshes/minimal_cube.mesh.txt`.
  - `CMakeLists.txt` (root) — read in full. Confirmed the exact current
    `add_subdirectory()` ordering and its own documented constraints
    (D9's `add_dependencies()` ordering rule, Plan 0013's own precedent).
  - `tests/image_regression/{CMakeLists.txt,fixture/*,golden_generator/main.cpp,support/provenance.h}`
    and the existing golden's own sidecar file — read in full. Confirmed
    the exact `MinimalCubeFixture`/`setUpMinimalCubeFixture()`/
    `renderOneFrame()` shape (including its own `RenderGraphBuilder`-based
    offscreen-readback copy pass — `Renderer::drawFrame()` alone does not
    perform this), the golden generator's exact git/provenance/PNG-write
    sequence, and the sidecar's exact field set — directly informing D10
    below.
  - `tests/runtime/runtime_ownership_tests.cpp` — confirmed the existing
    `static_assert`-based move-only-type test pattern this Plan's own new
    tests follow.
  - [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)'s
    own Decision and Accepted Amendment — re-read in full to fix D10's own
    exact "Initial baseline bootstrap" compliance procedure against the
    Amendment's own six constraints, not summarized from memory.

  This review found no case where an `Accepted` decision in Spec 0014 or
  ADR-0048–0051 is not implementable exactly as written against the real,
  current source tree — no objection to any `Accepted` decision is raised
  by this Plan. One genuine implementability gap the Spec/ADRs left open
  is resolved here for the first time, evidence-grounded rather than
  guessed: **how Runtime obtains `minimal_cube`'s real `AssetId`** without
  any change to Asset System's public API or CMake surface — see D6.
  A second, real gap was **found, not solved**, while drafting this
  Plan's own verification matrix: `EntityId` carries no `World`-instance
  identity — resolved, not merely disclosed, by Independent Review Round
  2 below.
- **Independent Review — Round 2 (2026-08-22):** A second, more rigorous
  pass, covering seven specific points; six resolved directly in this
  document, one escalated as blocking:
  1. **Cross-`World`-instance `EntityId` use — BLOCKING, escalated, not
     Plan-resolved.** Round 1's own framing ("not reachable by any code
     this Plan writes, since Runtime constructs exactly one `World`
     instance") is exactly the reasoning that must not stand in for a
     public module's own correctness — a public module's safety contract
     must hold independent of which one consumer exists today. Re-checked
     against ADR-0049's full current text: every one of its guarantees
     (stale-handle detection, permanent retirement) is proven relative to
     **one** `World` instance's own mutation history; nothing states this
     as a precondition on `EntityId` itself, and the only adjacent text
     (Spec 0014's/ADR-0048's "Runtime owns the one real `World` instance"
     sentence) describes Runtime's own composition choice, not a stated
     `EntityId` contract. Concretely, two freshly constructed `World`
     instances both hand out `{index=0, generation=0}` for their own
     first entity — a guaranteed, not merely coincidental, cross-instance
     collision on the most common possible sequence. This is a real gap
     in the `Accepted` contract, not a Plan-stage detail: **a Proposed
     Amendment is appended to ADR-0049** (recommending — not silently
     deciding — that cross-instance use become a documented, unenforceable
     precondition violation, no `EntityId` shape change) and Plan approval
     is blocked until Human Review accepts or otherwise resolves it — see
     the amendment itself and Deviations below.
  2. **`World` copy/move semantics — resolved.** Neither Spec 0014 nor
     ADR-0048–0051 stated this. Fixed in D3: move-constructible (required
     for `RuntimeApplication`'s own existing move-constructibility;
     previously issued `EntityId`s remain valid across a move, since a
     `std::vector`-based move preserves `slots_` exactly and `EntityId`
     never references a `World` object's own address), not copyable and
     not move-assignable (both would let two live `World` "identities"
     share state that validates the same `EntityId`s, a concrete instance
     of the same hazard point 1 discusses — deleting them removes it
     rather than leaving it latent). New V22.
  3. **Recursive `updateTransforms()` — resolved.** The original design's
     C++ call-stack usage was O(hierarchy depth) with no stated bound —
     a real, not theoretical, stack-overflow risk for a legal, sufficiently
     deep chain. D6 now uses a fully iterative traversal (an explicit,
     heap-allocated work list, no recursive function), removing the
     stack-depth risk entirely rather than adding an unapproved artificial
     depth cap.
  4. **Childless cascading destroy — re-verified, no bug found.** Explicit
     hazard-by-hazard re-verification added to D3: the forest-shaped
     hierarchy (one parent per entity, `setParent()`'s own cycle check)
     rules out double-processing; the two-phase collect-then-mutate
     structure (no slot is touched until collection fully completes, and
     this Spec's own single-threaded model precludes any interleaved
     `createEntity()`) rules out a missed descendant or a set that changes
     mid-scan from slot reuse. The original algorithm was already correct;
     this round strengthens the Plan's own written justification for why.
  5. **Camera degenerate-input coverage — re-verified, already adequate.**
     V15 (`scene_extraction_tests.cpp`, GPU-independent) already covers
     well-formed shear, near-zero forward, forward parallel to world-up,
     and negatively-scaled ancestors — confirmed this is the load-bearing
     proof; V20 (manual windowed) is visual confirmation only, never a
     substitute, now stated explicitly in V20's own row.
  6. **Golden commit ordering — re-verified, already correct.** D10's
     ADR-0042 compliance procedure and Step 7/Step 8's split (code first,
     golden capture as a separate, subsequent commit, existing
     `minimal_cube` golden untouched) already satisfy this; no change
     needed beyond the atomicity-wording clarification in point 7.
  7. **"Atomic" step labels — narrowed.** Of ten steps, three (Steps 1, 5,
     7) were marked atomic; re-audited against the actual bar (would
     splitting break configure/build, or violate a provenance rule) —
     none of the three, as originally justified, met that bar (each
     bundled more than the minimal buildable/testable unit for
     convenience, not necessity). Steps 1/5/7's atomic labels are removed
     and replaced with an explicit statement of the one genuine mechanical
     minimum each involves, with Implementation free to choose its own
     commit granularity above that minimum. Step 8 remains a mandatory
     **separate** commit — a provenance rule (ADR-0042), a different kind
     of constraint than "must not be split," now stated distinctly so the
     two are not conflated.
- **Independent Review — Round 3 (2026-08-22): Human Review direction on
  the ADR-0049 amendment, applied.** Human Review rejected Round 2's own
  recommended Option A (documented, unenforceable UB) and directed Option
  B, with a specific mechanism: a **stable, per-`World`, heap-allocated,
  address-stable identity token** — explicitly not a global, process-wide
  instance counter. This round applies that direction concretely:
  - `EntityId` gains a third field, `worldIdentity` (a non-owning pointer
    to an opaque, private `WorldIdentity` marker type `World` allocates
    once via `std::unique_ptr` at construction) — superseding the prior
    16-byte shape; the exact resulting size is no longer a number this
    Plan (or the amending ADR/Spec) fixes (D2).
  - Every `EntityId`-accepting `World` method now routes through a single
    `validate()` helper checking identity **before** slot/generation,
    with an explicit `worldIdentity == nullptr` carve-out so the existing
    sentinel `kInvalidEntityId` keeps reporting `InvalidEntity`, never the
    new `WrongWorld` (D3, D4, D5).
  - `WorldError` gains its fourth enumerator, `WrongWorld` (D2, D11).
  - `World`'s copy/move semantics (D3, fixed in Round 2) are now
    load-bearing for identity, not merely a convenience choice: moving a
    `World` moves its identity token's own heap block intact (same
    address before and after), so a handle valid before the move remains
    valid after it — the *reason* copy/move-assignment are refused is now
    tied directly to this mechanism, not only the general "byte-identical
    state" argument Round 2 used.
  - Three new verifications added — V23 (two live `World`s' own first
    entities correctly cross-reject via `WrongWorld`, not coincidental
    validation), V24 (`WrongWorld` reachable from every entry point, not
    only `isValid()`), V25 (a handle issued before a `World` move remains
    valid after it) — and V2 is restored to its own clean, same-instance-
    only scope now that cross-instance behavior has a real, dedicated
    home.
  - **Both [ADR-0049](../adr/0049-entity-identity-and-handle-invalidation.md)'s
    and [Spec 0014](../specs/0014-world-scene-foundation.md)'s own
    Proposed Amendments record this design in full, including the
    alternatives Human Review explicitly rejected alongside it (a global
    incrementing counter, `shared_ptr`/`weak_ptr`-based identity,
    forbidding multiple `World` construction) — neither amendment is
    marked `Accepted` by this round; that remains a distinct, later
    Human Review action.** No conflict between this mechanism and any
    already-`Accepted` ownership rule was found — see the ADR's own
    amendment for why a zero-data, comparison-only opaque pointer does
    not re-open [ADR-0033](../adr/0033-runtime-authority-and-client-boundary.md)'s
    "no raw pointer to Runtime-owned state" rule.
- **Related ADR(s):**
  [ADR-0048](../adr/0048-world-scene-module-boundary-and-ownership.md)–[ADR-0051](../adr/0051-world-to-renderer-extraction-and-asset-resolution-boundary.md),
  all `Accepted` 2026-08-22, **except** ADR-0049's new "Proposed
  Amendment" section (2026-08-22), which is `Proposed`, not yet
  `Accepted` — see Independent Review Round 2, point 1, and Deviations.

## Objective

Implement Spec 0014 in full: a new, eleventh top-level module,
`Atlantis::World`, giving Atlantis its first in-memory, multi-entity
scene — Entity lifecycle with a formally-overflow-safe index+generation
handle, fixed-type `Transform`/`Camera`/`Renderable` component storage,
an atomic parent/child hierarchy with cycle prevention and cascading
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

These are the details Spec 0014 and ADR-0048–0051 explicitly leave to
Plan stage. Each is decided here so Implementation has nothing
architectural left to choose.

### D1. CMake targets, namespace, directories, dependency graph

| Target | Kind | Location | Links | Notes |
|---|---|---|---|---|
| `atlantis_world` (alias `Atlantis::World`) | STATIC | `src/world/` | **PUBLIC** `Atlantis::Core`, **PUBLIC** `Atlantis::AssetSystem`; PRIVATE `atlantis_compiler_warnings` | `Atlantis::AssetSystem` is **PUBLIC**, not PRIVATE: `Renderable`'s own public header names `atlantis::asset_system::AssetId` in a field type, so any consumer of `<atlantis/world/renderable.h>` must transitively see that type — matching how `Atlantis::ShaderSystemRhiIntegration` already links `Atlantis::RHI` PUBLIC for the identical reason (a public type in its own public header). No other dependency, in either direction — verified by a module-boundary test mirroring `tests/asset_system/module_boundary_tests.cpp`'s own precedent (V16). |
| `atlantis_world_tests` | executable | `tests/world/` | `Atlantis::World`, `Catch2::Catch2WithMain`, `atlantis_compiler_warnings` | GPU-independent — no `Device`, no GPU, no real window; links no RHI/Renderer/Vulkan Backend/Platform target at all. |

- **Namespace:** `atlantis::world`, matching [AGENTS.md](../AGENTS.md)'s
  C++ coding conventions.
- **Public header root:** `src/world/include/atlantis/world/`, matching
  every other real module's own layout (Core, RHI, Renderer, Asset
  System, Runtime all use this exact pattern).
- **One primary type per header/source pair** (AGENTS.md's own file
  convention): `vec3.h`, `entity_id.h`, `world_error.h`, `transform.h`,
  `camera.h`, `renderable.h` are each a single small value type with no
  `.cpp` of their own (trivial aggregates, no out-of-line member
  functions needed); `world.h`/`world.cpp` hold the `World` class itself,
  the only type in this module with real behavior.
- **Root `CMakeLists.txt` ordering:** `add_subdirectory(src/world)` is
  inserted immediately after `add_subdirectory(src/asset_system)` (line
  44 in the current file) and before `add_subdirectory(src/platform)` —
  the earliest point both of `Atlantis::World`'s own dependencies
  (`Atlantis::Core`, `Atlantis::AssetSystem`) are already declared,
  matching this repository's own established convention (confirmed
  against the real file: RHI/RenderGraph are already `add_subdirectory()`-d
  before Renderer, which links both) of declaring a dependency's own
  target before any target that links it. `add_subdirectory(tests/world)`
  is inserted immediately after `add_subdirectory(tests/asset_system)` in
  the `ATLANTIS_BUILD_TESTS` block, mirroring the same relative ordering.
  `add_subdirectory(src/runtime)` (already present, later in the file)
  needs no ordering change of its own — `src/world` lands well before it.

### D2. Public value types — exact C++ shapes

Each header below is the **entire** contents of its own module's public
contract for that type — nothing here is left for Implementation to
invent, per Spec 0014's own Requirements and ADR-0049/0050, as amended by
[ADR-0049's own Proposed Amendment](../adr/0049-entity-identity-and-handle-invalidation.md)
(stable `World` identity token; Human Review direction recorded
2026-08-22, pending final Accept).

```cpp
// vec3.h
struct Vec3 {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

// entity_id.h
namespace atlantis::world {
class WorldIdentity;  // opaque forward declaration only -- full definition
                       // private to world.cpp; EntityId never dereferences it

struct EntityId {
  std::uint32_t index = std::numeric_limits<std::uint32_t>::max();
  std::uint64_t generation = 0;
  const WorldIdentity* worldIdentity = nullptr;

  friend bool operator==(const EntityId&, const EntityId&) = default;
};
inline constexpr EntityId kInvalidEntityId{};  // index == max, generation == 0, worldIdentity == nullptr
}  // namespace atlantis::world

// world_error.h
enum class WorldError {
  InvalidEntity,       // stale or out-of-range handle, or the invalid sentinel
  WouldCreateCycle,     // setParent() would make child its own ancestor
  NoCameraComponent,    // setActiveCamera() target has no Camera
  WrongWorld,            // handle's worldIdentity belongs to a different, live World instance
};

// transform.h
struct Transform {
  Vec3 localPosition{};
  Vec3 localEulerAnglesRadians{};   // pitch (x), yaw (y), roll (z)
  Vec3 localScale{1.0f, 1.0f, 1.0f};
};

// camera.h
struct Camera {
  float fovYRadians = 0.0f;
  float nearZ = 0.0f;
  float farZ = 0.0f;
};

// renderable.h
struct Renderable {
  atlantis::asset_system::AssetId meshAsset = 0;
};
```

- `EntityId`'s default constructor already equals `kInvalidEntityId` (all
  three members default-initialize to the sentinel values) —
  `EntityId{}` and `kInvalidEntityId` are interchangeable; the named
  constant exists purely for readability at call sites.
- **`EntityId` is 24 bytes on a typical 64-bit target** (`uint32_t` index
  padded to 8, `uint64_t` generation, one pointer) — up from the prior
  16-byte shape; the *exact* figure is target-pointer-width- and
  alignment-dependent and is not a number this Plan (or the amending ADR)
  fixes, only the field list above is fixed. No type above declares a
  destructor or any method beyond `EntityId`'s defaulted `operator==`
  (which now compares all three fields, `worldIdentity` included,
  satisfying ADR-0049's own amendment requirement that equality include
  identity with no separately written comparison code); every one remains
  a trivial, standard-layout aggregate, freely copyable, safe to store in
  `std::vector`. `worldIdentity` is a plain observer pointer — `EntityId`
  never allocates, frees, or dereferences anything through it, unchanged
  in *kind* from `index`/`generation` already being non-owning values.

### D3. `World`'s internal slot map — exact representation

Private to `world.cpp` (not declared in any public header):

```cpp
struct WorldIdentity {};  // opaque, no data, no behavior -- only its own
                           // heap address matters, as this instance's
                           // unique, address-stable token (ADR-0049's
                           // own Proposed Amendment)

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
  World();   // defined in world.cpp (not = default inline) -- allocates
             // identity_ via std::make_unique<WorldIdentity>(), which
             // requires WorldIdentity's complete definition, private to
             // this file
  ~World();  // defined in world.cpp for the identical reason -- unique_ptr's
             // destructor also requires WorldIdentity to be complete
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
(D2) also only forward-declares it — neither header needs the complete
type, since both only ever hold or compare a pointer to it, never
dereference it. The complete, one-line definition (`struct
WorldIdentity {};`) lives in `world.cpp` alone, alongside `Slot`. This is
the standard C++ idiom for an opaque-pointer member; it is what forces
`World`'s own constructor and destructor to be declared in the header but
**defined** in `world.cpp` (`= default` bodies suffice for both) rather
than inlined in the class body, since `std::make_unique<WorldIdentity>()`
(in the constructor) and `unique_ptr`'s own implicit `delete` (in the
destructor) both require `WorldIdentity` to be complete at the point they
are compiled.

**`World`'s own copy/move semantics — fixed here; Spec 0014/ADR-0048–0051
left this genuinely open, it is not an oversight to state it now. Now
directly load-bearing for identity, not merely a convenience choice**
(ADR-0049's own Proposed Amendment; Human Review direction 2026-08-22).
Move-constructible, **not** move-assignable, **not** copyable — the same
"move-construction-only" shape `PlatformSession`/`RuntimeApplication`
already establish as this codebase's own precedent for a type with real
per-instance identity:

- **Not copyable.** A copy constructor would have to choose between
  sharing the source's own `identity_` token (defeating the entire point
  — two live `World`s would then validate the same handles, exactly the
  hazard this design exists to close) or minting a fresh one via its own
  `make_unique` call (which would make every `EntityId` copied over from
  the source's own component data silently `Err(WrongWorld)` against the
  copy, an unpleasant surprise with no good default). No choice is right,
  so copying is deleted outright — `slots_`/`freeList_`/`activeCamera_`
  themselves would copy safely at the C++ level; `identity_` is the
  entire reason copying is refused.
- **Move-constructible.** `RuntimeApplication` holds `world_` as a plain
  value member (D8) and is itself move-constructible (Spec 0013's own
  already-`Accepted` pattern, unchanged by this Plan) — its compiler-
  generated move constructor requires every member, `World` included, to
  be move-constructible. Moving `identity_` (a `std::unique_ptr`)
  transfers ownership of the **same** underlying `WorldIdentity` heap
  block — that object's own address never changes across the move, only
  which `World` C++ object owns the `unique_ptr` pointing to it. A
  `std::vector`-based move likewise transfers the underlying heap
  allocation without touching element values. Together, `identity_.get()`
  returns the **exact same address** before and after the move, and
  `slots_`/`freeList_`/`activeCamera_` are preserved exactly — so any
  `EntityId` valid before the move (`worldIdentity` equal to the
  pre-move `identity_.get()`, index/generation matching a preserved slot)
  remains valid after it, against the moved-to instance, with no special
  handling required anywhere in the validation logic.
- **Not move-assignable.** Move-assignment (`worldA =
  std::move(worldB)`) would free `worldA`'s own `identity_` token and
  replace it with `worldB`'s, while any `EntityId` a caller still holds
  against "the `World` that used to be reachable as `worldA`" remains
  stamped with the now-freed token's own (dangling) address — a real
  lifetime hazard distinct from, and not solved by, the `WrongWorld`
  check (which only helps between two *simultaneously live* instances).
  `RuntimeApplication` itself does not need `World`'s move-assignment
  (its own move-assignment is already deleted, matching Spec 0013's own
  precedent), and no other consumer in this Spec's own scope needs it
  either — deleted rather than left as a latent hazard with no real use.
- A `static_assert`-based test (matching `runtime_ownership_tests.cpp`'s
  own established pattern) confirms exactly this shape:
  `std::is_move_constructible_v<World> && !std::is_copy_constructible_v<World>
  && !std::is_move_assignable_v<World> && !std::is_copy_assignable_v<World>`
  (V22).

- **`validate(id)` — private helper, identity checked before
  slot/generation, every `EntityId`-accepting method routes through it
  (D4/D5 as well as D3):**

  ```cpp
  Result<void, WorldError> World::validate(EntityId id) const {
    if (id.worldIdentity != nullptr && id.worldIdentity != identity_.get())
      return Result<void, WorldError>::Err(WorldError::WrongWorld);
    if (id.index >= slots_.size() || !slots_[id.index].alive
        || slots_[id.index].generation != id.generation)
      return Result<void, WorldError>::Err(WorldError::InvalidEntity);
    return Result<void, WorldError>::Ok({});
  }
  ```

  The `id.worldIdentity != nullptr` guard is deliberate: `kInvalidEntityId`
  (`worldIdentity == nullptr`) is "no claimed identity," not "a foreign
  instance's identity" — without the guard, the existing sentinel would
  incorrectly report `WrongWorld` instead of its own unchanged
  `InvalidEntity` classification. A real handle from a **different, live**
  `World` (non-null, non-matching pointer) correctly reaches `WrongWorld`
  before either the index or generation is even inspected; every other
  case reaches the existing, unmodified index/generation check exactly as
  before this amendment.
- **`createEntity()`:** if `freeList_` is non-empty, `pop_back()` its
  index (the LIFO rule — ADR-0049's own Decision); otherwise
  `slots_.emplace_back()` and use the new highest index. Either way, set
  `alive = true`, reset `parent`/`localTransform`/`camera`/`renderable`
  to their default-constructed values (a reused slot's own generation is
  **not** touched here — it was already advanced by the `destroyEntity()`
  call that freed it), and return `EntityId{index, slots_[index].generation,
  identity_.get()}` — every handle this instance ever issues is stamped
  with this instance's own token. Never fails.
- **`destroyEntity(id)` — cascading, via a worklist, no stored children
  list:** `validate(id)` first (`Err(WrongWorld)` or `Err(InvalidEntity)`,
  zero mutation, if invalid). On success, build `std::vector<EntityId>
  toDestroy{id}` and,
  for each entry already in the list (index-based loop so appending
  during iteration is safe with `std::vector`), scan every **alive**
  slot whose `parent` equals that entry and append it — an O(N × depth)
  scan, deliberately not a stored bidirectional child-list (matching
  ADR-0050's own "no premature optimization" precedent for
  `updateTransforms()`; at this Spec's own validation-scale entity count,
  the cost is trivial). **Only after this collection phase is fully
  complete** does a second loop actually mutate any slot: for every
  entity in `toDestroy` (order immaterial — no per-entity cleanup beyond
  flag/counter mutation depends on visiting order): if `activeCamera_`
  equals it, clear `activeCamera_` to `std::nullopt`; mark the slot dead
  (`alive = false`); increment `generation` (see the tombstone rule
  below); if the post-increment value is **not** the tombstone,
  `freeList_.push_back(index)` — if it **is** the tombstone, the index is
  never pushed, permanently retiring it (ADR-0049's own Decision).

  **Correctness of this two-phase (collect-then-mutate) structure,
  verified explicitly, not merely asserted — re-checked during Plan
  Review Round 2 against three specific hazards (miss a descendant,
  process one twice, or have slot reuse change the set mid-scan):**
  - **No missed descendant:** the hierarchy is a forest — `setParent()`'s
    own cycle check (D5) guarantees each entity has exactly one parent
    and no entity is its own ancestor — so every descendant is reachable
    by exactly one path of direct-child links from `id`, and the
    index-based `for` loop (re-evaluating `toDestroy.size()` each
    iteration, a well-defined pattern for an appending `std::vector`)
    visits every entry ever appended, including ones appended during the
    same loop, so the scan reaches every depth, not just direct children.
  - **No double-processing:** because the hierarchy is a forest (not a
    DAG), an entity has exactly one parent, so it can be discovered by
    the scan at most once (via that one parent's own turn in the loop) —
    there is no second path that could append the same `EntityId` twice.
  - **Slot reuse cannot change the set mid-scan:** `current = toDestroy[i]`
    copies the `EntityId` **by value** before any further work in that
    iteration, so a later `push_back()` on `toDestroy` (which may
    reallocate `toDestroy`'s own buffer) never invalidates a reference
    `current` might otherwise have held. More fundamentally, **no slot is
    mutated until the entire collection phase has finished** — the scan
    condition (`slots_[j].alive && slots_[j].parent == current`) is
    evaluated against the exact same, unchanged `slots_` state throughout
    collection, and this Spec's own single-threaded model (no concurrent
    `createEntity()`/`destroyEntity()` call can interleave with this one)
    means no other code can reuse a slot mid-scan even in principle. The
    `.alive` guard additionally makes the second (mutation) phase safe
    regardless of whether a dead slot's own stale `parent` field (not
    reset until the slot is reused by a future `createEntity()`) is ever
    read again — a dead slot is always excluded from the scan condition
    regardless of what its `parent` field still holds.
- **Generation-retirement tombstone, exact check:** after
  `++slot.generation`, `if (slot.generation ==
  std::numeric_limits<std::uint64_t>::max()) { /* retired; do not push
  to freeList_ */ }` — a single equality check, no separate "near the
  edge" threshold (ADR-0049's own Alternatives Considered already
  rejected an early-cutoff variant).
- **`isValid(id)`:** `(id.worldIdentity == nullptr || id.worldIdentity ==
  identity_.get()) && id.index < slots_.size() && slots_[id.index].alive
  && slots_[id.index].generation == id.generation` — the same two-part
  check `validate()` performs, collapsed to a `bool`; a handle from a
  different, live `World` and an ordinary stale/out-of-range handle both
  simply report `false`, unchanged in signature and existing callers'
  expectations (the finer `WrongWorld`-vs-`InvalidEntity` distinction is
  only surfaced by the `Result`-returning API, per D4).

### D4. Component and Transform accessor API — exact signatures, atomic by construction

Every `EntityId`-accepting method — `destroyEntity()`, every setter/
getter below, and `setParent()`/`getParent()` (D5) — routes through the
same `validate(id)` helper (D3) **first**: identity checked before
slot/generation, `Err(WorldError::WrongWorld)` before `Err(InvalidEntity)`
is even considered. Every setter additionally validates — and, for
`setParent()`, runs the cycle check — **before** writing any member, so a
`Result::Err` return is always zero-mutation by construction (ADR-0049's own blanket
atomicity contract); no explicit "rollback" code exists anywhere because
nothing is ever written before every precondition has already passed.

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

  // Ascending slot-index order (ADR-0049's own Decision) -- a fresh
  // std::vector snapshot each call, valid as of the call, not a live
  // iterator held across a subsequent World mutation.
  [[nodiscard]] std::vector<EntityId> renderableEntities() const;
};
```

- **`getParent(child)`** returns `Ok(kInvalidEntityId)` for a root entity
  — not an error; only a `WrongWorld`/stale `child` handle is `Err`.
- **`setActiveCamera(id)`** runs `validate(id)` first (`WrongWorld` or
  `InvalidEntity`) then checks `slots_[id.index].camera.has_value()`
  (`NoCameraComponent`) before writing `activeCamera_ = id`.
- **`renderableEntities()`** walks `slots_` in ascending index order,
  appending `EntityId{index, slot.generation, identity_.get()}` for every
  slot with `alive && renderable.has_value()`.

### D5. `setParent()` — cycle check, exact algorithm

```cpp
Result<void, WorldError> World::setParent(EntityId child, EntityId parent) {
  if (auto r = validate(child); r.isErr()) return r;
  if (parent != kInvalidEntityId) {
    if (auto r = validate(parent); r.isErr()) return r;
    // Walk parent's own ancestor chain, including parent itself as the
    // zeroth step -- this single loop covers both "parent == child"
    // (the degenerate one-entity cycle) and every longer transitive
    // cycle with one algorithm, not two special cases.
    EntityId ancestor = parent;
    while (ancestor != kInvalidEntityId) {
      if (ancestor == child) return Result<void, WorldError>::Err(WorldError::WouldCreateCycle);
      ancestor = slots_[ancestor.index].parent;
    }
  }
  slots_[child.index].parent = parent;
  return Result<void, WorldError>::Ok({});
}
```

No state is written until the function's final line — every `Err` path
(`WrongWorld`/`InvalidEntity` from either handle, or `WouldCreateCycle`)
returns before any mutation, satisfying atomicity trivially.

### D6. Math contract — exact matrix construction, and how Runtime obtains `minimal_cube`'s real `AssetId`

**Matrix layout and composition** (ADR-0050's own Math contract,
restated here as concrete code, not re-decided): column-major
`std::array<float, 16>`, index `col * 4 + row`; `kIdentityMatrix4 =
{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}`; `multiply(a, b)` implements
`result = a · b` for that layout — **the exact same element formula**
`examples/minimal_renderer_demo/main.cpp`'s own `multiply()` already
uses (`result[col*4+row] = Σ_k a[k*4+row] * b[col*4+k]`), reused
verbatim as `atlantis::world`'s own private implementation, not called
across the module boundary.

**Per-axis rotation matrices** (right-handed, column-vector, matching
ADR-0050's own stated `Ry` example exactly):

```cpp
Mat4 rotationX(float theta) {  // rotates Y/Z about +X
  const float c = std::cos(theta), s = std::sin(theta);
  return {1,0,0,0,  0,c,s,0,  0,-s,c,0,  0,0,0,1};
}
Mat4 rotationY(float theta) {  // rotates X/Z about +Y
  const float c = std::cos(theta), s = std::sin(theta);
  return {c,0,-s,0,  0,1,0,0,  s,0,c,0,  0,0,0,1};
}
Mat4 rotationZ(float theta) {  // rotates X/Y about +Z
  const float c = std::cos(theta), s = std::sin(theta);
  return {c,s,0,0,  -s,c,0,0,  0,0,1,0,  0,0,0,1};
}
Mat4 eulerRotation(Vec3 radians) {  // R = Ry(yaw) * Rx(pitch) * Rz(roll)
  return multiply(rotationY(radians.y), multiply(rotationX(radians.x), rotationZ(radians.z)));
}
Mat4 composeLocal(const Transform& t) {  // local = T * R * S
  return multiply(translationMatrix(t.localPosition),
                   multiply(eulerRotation(t.localEulerAnglesRadians), scaleMatrix(t.localScale)));
}
```

`translationMatrix`/`scaleMatrix` are the obvious identity-plus-one-field
variants; not reproduced here for brevity, fixed in shape by the
comment above.

**`updateTransforms()` — memoized, fully iterative traversal (no C++
recursion, no call-stack depth tied to hierarchy depth), doubling as the
defense-in-depth cycle guard:**

Plan Review Round 2 replaced this section's original design (per-entity
memoized *recursion*, one C++ call-stack frame per ancestor-chain level)
with an iterative one. The original design's own C++ call-stack usage
was O(hierarchy depth) with no stated or enforced bound; nothing in Spec
0014/ADR-0050 caps hierarchy depth, so an unbounded-but-otherwise-legal
deep chain (e.g. a long accidental or generated parent chain) could
exhaust the call stack — a real robustness gap, not merely a theoretical
one, since `World` places no limit on how deep a caller may nest
`setParent()` calls. The traversal's own internal order was never part
of World's public contract (ADR-0050's own Consequences; only the
topological "parent before child" property is), so replacing recursion
with an iterative algorithm producing the same topological order changes
no `Accepted` decision.

```cpp
void World::updateTransforms() {
  for (auto& slot : slots_) slot.visitState = SlotVisitState::NotVisited;
  std::vector<std::uint32_t> path;  // reused scratch buffer -- heap-allocated, not call-stack depth
  for (std::uint32_t i = 0; i < slots_.size(); ++i) {
    if (!slots_[i].alive || slots_[i].visitState == SlotVisitState::Visited) continue;
    // Walk up from i, collecting the not-yet-visited prefix of its own
    // ancestor chain, stopping at a root or at an already-Visited
    // ancestor (whose own cachedWorldMatrix is already correct).
    path.clear();
    std::uint32_t current = i;
    while (true) {
      Slot& s = slots_[current];
      if (s.visitState == SlotVisitState::Visited) break;
      ATLANTIS_CHECK_MSG(s.visitState != SlotVisitState::Visiting,
                          "updateTransforms(): cycle detected -- setParent()'s own prevention has a bug");
      s.visitState = SlotVisitState::Visiting;  // "on the current walk-up path" -- the cycle guard
      path.push_back(current);
      if (s.parent == kInvalidEntityId) break;
      current = s.parent.index;
    }
    // Process outermost-unvisited-ancestor-first, so each entity's own
    // parent world matrix is already known by the time it is computed --
    // an ordinary loop, no recursive call.
    for (auto it = path.rbegin(); it != path.rend(); ++it) {
      Slot& s = slots_[*it];
      const std::array<float, 16> local = composeLocal(s.localTransform);
      s.cachedWorldMatrix = (s.parent == kInvalidEntityId)
          ? local
          : multiply(slots_[s.parent.index].cachedWorldMatrix, local);  // parent, if set, was
                                                                          // just processed above,
                                                                          // or was already Visited
      s.visitState = SlotVisitState::Visited;
    }
  }
}
```

`path` grows on the heap (an ordinary `std::vector`, reused across outer
iterations to avoid repeated allocation), so its size is bounded only by
available memory, not the C++ call stack — the same class of resource
`slots_` itself already uses, not a new kind of allocation this codebase
does not already rely on elsewhere. No depth cap is introduced: Spec
0014/ADR-0050 never bounded hierarchy depth, and adding an artificial
cap now would itself be a new, un-`Accepted` restriction on `World`'s
public behavior — the iterative rewrite removes the stack-depth risk
without adding one.

Any traversal order satisfying "visit a parent before its child"
produces identical results — this per-entity memoized iteration is one
such order; its own internal visitation order remains outside World's
public contract (ADR-0050's own Consequences), only the topological
property is.

**How Runtime obtains `minimal_cube`'s real `AssetId` — resolved here,
grounded in real code, not designed in the abstract:** `StaticMeshAssetData`
(returned by the already-`Accepted` `loadStaticMeshAsset()`) carries no
`AssetId` field. Asset System's own already-`Accepted`, already-public
`asset_metadata.h` does: `parseAssetMetadata(std::string_view) ->
Result<AssetMetadata, MetadataParseError>`, and `AssetMetadata::assetId`
is exactly the real, cooked `AssetId` for that artifact. Runtime therefore
reads `config.assetMetadataPath`'s own file contents as plain text (a
second, small read of the same file `loadStaticMeshAsset()` already
consumes — no new I/O mechanism, matching the existing `loadSpirvFile()`-
style plain-`std::ifstream` pattern already used throughout this file) and
calls `parseAssetMetadata()` on it directly, storing the resulting
`assetId` in a new `RuntimeApplication` member,
`atlantis::asset_system::AssetId knownMinimalCubeAssetId_`. **No change
to any Asset System header, `.cpp`, or `CMakeLists.txt`** — both functions
this uses already exist, `Accepted`, unmodified; this is new call-site
code in `src/runtime/` only.

### D7. Runtime's own camera-matrix extraction and asset resolution — new, independently-testable files

A new file pair, `src/runtime/include/atlantis/runtime/scene_extraction.h`
/ `src/runtime/src/scene_extraction.cpp`, factors two pieces of pure,
GPU-independent logic out of `runtime_application.cpp`'s own anonymous
namespace so they are unit-testable per Spec 0014's own Testing &
Verification Plan (`tests/runtime/`, "no `Device`, no GPU, no `World`
instance required"). This is a **refactor of Runtime's own private
helpers**, not a new public API surface (`atlantis::runtime` remains
Runtime's own internal namespace; nothing here is consumed by any other
module) and not a shared "Extraction module" between Runtime and the new
image-regression fixture — ADR-0051's own Alternatives Considered
already forecloses that; the new image-regression fixture (D10)
duplicates this same logic independently, matching the codebase's own
established "duplicated, not shared" precedent for exactly this kind of
small camera-math helper (`lookAt()`/`perspective()`/`identityMatrix()`
already exist, separately, in `runtime_application.cpp`,
`minimal_cube_fixture.cpp`, and `examples/minimal_renderer_demo/main.cpp`
today).

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
// column -- ADR-0051's own Decision step 3), feeds them into the same
// lookAt()-shaped construction every existing composition root already
// uses, and builds the projection matrix from fovYRadians/nearZ/farZ and
// the caller-supplied aspect ratio. Detects both degenerate-input cases
// explicitly (kDegenerateLengthEpsilon = 1e-6f, chosen because this
// codebase's own scenes operate at a roughly 1-10 world-unit scale --
// see Deviations for why this exact value is not further tuned) before
// ever calling lookAt(), so that function is never invoked with an input
// that would make it divide by a near-zero length.
[[nodiscard]] atlantis::Result<CameraMatrices, SceneExtractionError> extractCameraMatrices(
    const Mat4& cameraWorldMatrix, float fovYRadians, float nearZ, float farZ, float aspect);

// Trivial by design (ADR-0051's own Decision step 4 fixes only the
// existence and input/output shape of asset resolution, not a container)
// -- this Plan's own validation scene resolves against exactly one known
// AssetId; Ok() on a match, Err(UnresolvedMeshAsset) otherwise.
[[nodiscard]] atlantis::Result<std::monostate, SceneExtractionError> resolveMeshAsset(
    atlantis::asset_system::AssetId requested, atlantis::asset_system::AssetId known);

}  // namespace atlantis::runtime
```

`extractCameraMatrices()`'s own internal steps, precisely: (1) `forward
= normalize(-column 2 of cameraWorldMatrix)`; if the pre-normalization
length is `< kDegenerateLengthEpsilon`, return
`Err(DegenerateCameraForward)`. (2) `eye = column 3 (translation) of
cameraWorldMatrix`. (3) `right = cross(forward, {0,1,0})`; if its own
pre-normalization length is `< kDegenerateLengthEpsilon`, return
`Err(DegenerateCameraBasis)` — this check runs **before** calling
`lookAtMatrix()`, which performs the identical cross product internally
without a guard; the two are deliberately redundant only in the sense
that `lookAtMatrix()` is the same, unmodified function every existing
demo already calls, and this function's own pre-check is what prevents
it from ever being invoked with a degenerate input, not a change to
`lookAtMatrix()` itself. (4) On success, `view =
lookAtMatrix(eye.x,eye.y,eye.z, eye.x+forward.x,eye.y+forward.y,eye.z+forward.z)`,
`projection = perspectiveMatrix(fovYRadians, aspect, nearZ, farZ)`.

`resolveMeshAsset()` is `requested == known ? Ok(std::monostate{}) :
Err(UnresolvedMeshAsset)` — one comparison against the single known
`AssetId` D8 describes below (`knownMinimalCubeAssetId_`), not a
container lookup: this Plan's own validation scene resolves against
exactly one real asset (ADR-0051's own Decision step 4 fixes only the
resolution step's input/output shape, not a container type).

### D8. `RuntimeApplication` — new members, and per-frame extraction order

**New members** (added to the existing member list, `world_` declared
alongside `renderer_`/`lifecycle_` — none of it participates in the
fixed reverse-destruction-order sequence Spec 0013/ADR-0046 already
fixed, since `World` owns no GPU resource and has no ordering
relationship to `Device`/`Presentation`/`Mesh` at all):

```cpp
atlantis::world::World world_;
atlantis::asset_system::AssetId knownMinimalCubeAssetId_ = 0;
std::optional<atlantis::world::EntityId> activeCameraEntity_;  // cached for logging only; World itself is the source of truth
```

**`initializeSteps()` gains one new step, after the existing Step 4
(asset load) and before Step 5 (mesh creation) — reading the metadata a
second time for its `AssetId` (D6):** on `parseAssetMetadata()` failure,
the same teardown-and-exit path Step 4's own failure already uses,
returning a **new** `RuntimeInitError::AssetMetadataParseFailed`
enumerator (added to the existing `RuntimeInitError` enum — an additive
change to a type Runtime itself owns; not a change to any other module's
public API). After the existing Step 6 (camera buffer) and before
`lifecycle_.markRunning()`, a new step **builds the fixed validation
scene** (D9) via ordinary `world_.createEntity()`/`setLocalTransform()`/
`setParent()`/`setCamera()`/`setRenderable()`/`setActiveCamera()` calls —
every one of these already returns `Result`; a failure here (only
plausible from a genuine implementation bug, since every handle used is
one this same code just created) is treated identically to every other
`initializeSteps()` failure: `lifecycle_.markFailed()`, return a new
`RuntimeInitError::SceneConstructionFailed`.

**`runFrame()`'s existing Step 7 (camera write + `DrawItem` build) is
replaced, not extended, by:**

1. `world_.updateTransforms()`.
2. `world_.activeCamera()` — `std::nullopt` is `SceneExtractionError::NoActiveCamera`,
   treated as **unrecoverable** (`lifecycle_.markFailed()`; return) —
   this Plan's own validation scene always sets one at construction, so
   reaching this path indicates a genuine construction bug, the same
   "should never happen in correct operation" category Spec 0013 already
   established for a second `SurfaceCreated`/an unexpected
   `SurfaceDestroyed`.
3. `world_.getWorldMatrix(*activeCamera)` (guaranteed `Ok` — the handle
   was just read from `World` itself) →
   `extractCameraMatrices(cameraWorldMatrix, camera.fovYRadians,
   camera.nearZ, camera.farZ, aspect)` (D7). Any `SceneExtractionError`
   here is likewise **unrecoverable** (`markFailed()`; return) — the same
   reasoning: this Plan's own fixed camera Transform/`Camera` values are
   chosen (D9) to never be degenerate.
4. Write `view`/`projection` into `cameraBuffer_->mappedData()`, exactly
   as today.
5. Build `std::vector<DrawItem> drawItems` (a per-frame local, not a
   member — this Plan's own five-entity scale makes a per-frame
   allocation immaterial, matching the "not a goal beyond does not stall"
   Non-functional bar Spec 0014 itself sets): for each `EntityId` in
   `world_.renderableEntities()` (ascending-slot-index order — the order
   `drawItems` is built and submitted in): `world_.getRenderable(id)` →
   `resolveMeshAsset(renderable.meshAsset, knownMinimalCubeAssetId_)`
   (D7); on `Err(UnresolvedMeshAsset)`, log and **skip this one entity**
   (continue the loop) — **recoverable, per-entity**, deliberately a
   different policy than step 2/3's camera failures: a single bad
   reference should not halt an otherwise-valid scene, matching the
   general "keep going, log, retry/skip" philosophy the existing
   format-/extent-change retry logic already establishes. On success,
   `world_.getWorldMatrix(id)` (guaranteed `Ok`) and append
   `DrawItem{&*mesh_, &*material_, worldMatrix}`.
6. `device_->createCommandList()`, `renderer_.drawFrame(..., drawItems,
   ...)`, `device_->submit(...)`, `presentation_->present(...)` — **all
   four calls unchanged in shape**; `drawFrame()` already accepts
   `std::span<const DrawItem>`, so passing a `std::vector` of more than
   one item requires no new call shape, only more items in the same
   span.

**If `drawItems` is empty** (every entity's `AssetId` failed to resolve
— unreachable for this Plan's own scene, since every `Renderable` entity
is constructed with `knownMinimalCubeAssetId_` itself, but not
structurally impossible for a future scene): `Renderer::drawFrame()` is
still called with an empty span — `renderer.cpp`'s own `for` loop over
zero items is a legal no-op, so this produces a correctly-cleared,
empty-scene frame rather than a special-cased skip, requiring no new
branch.

### D9. The validation scene — exact entities, values, and why the camera is static

**Six entities, matching Spec 0014's own "several (e.g. five) plus one
Camera" Proposed Design exactly**, right-handed Y-up, world units at
roughly the same 1–10 scale every existing composition root already
uses (the cube itself is 1×1×1, per `assets/meshes/minimal_cube.mesh.txt`):

| Entity | Parent | Local position | Local rotation (rad, x/y/z) | `Renderable` | `Camera` |
|---|---|---|---|---|---|
| A | root | (−2.5, 0, 0) | (0, 0, 0) | `minimal_cube` | — |
| B | root | (−1.0, 0, 0) | (0, 0.5236 [30°], 0) | `minimal_cube` | — |
| C | root | (1.0, −0.5, 0) | (0, 0, 0) | `minimal_cube` | — |
| D | **C** | (0, 1.3, 0) | (0, 0.7854 [45°], 0) | `minimal_cube` | — |
| E | root | (2.5, 0, 0) | (0.2618 [15°], 0.3491 [20°], 0) | `minimal_cube` | — |
| F | root | (0, 2.2, 7.0) | (−0.3054 [−17.5°], 0, 0) | — | `{60° fovY, 0.1, 100.0}`, active |

D's world position ends up at `(1.0, 0.8, 0)` (C has no rotation of its
own, so D's local offset applies unchanged) — a visible, non-overlapping
child offset from its parent, exercising `setParent()`/hierarchy
composition without needing entity F's own camera framing to be pixel-
exact. F's own pitch (`−17.5°`) is chosen so its forward direction
(`(0, sin(−17.5°), −cos(−17.5°)) ≈ (0, −0.301, −0.954)`) points from
`(0, 2.2, 7.0)` back toward the cube row's own approximate centroid near
the world origin — a reasoned, not hand-verified-pixel-perfect,
framing; **visual fine-tuning of these exact numbers during
Implementation, to keep every cube comfortably inside the frame, is
expected and is not an architectural change** — see Deviations.

**No per-frame mutation of any entity's `Transform` — a deliberate,
Non-Goals-compliant choice, not an oversight.** Spec 0014's own Non-Goals
exclude "keyframe, blend, or time-driven mutation" from `World` itself;
this Plan's own validation scene is built once, in `initializeSteps()`,
and never touched again by `runFrame()` (`updateTransforms()` still runs
every frame, per Spec 0014's own contract, recomputing the same,
unchanged world matrices each time). "Moving/rotating a parent visibly
moves its child too" (Spec 0014's own Testing & Verification Plan
wording) is satisfied by D/C's own static, correctly-composed geometry
looking visibly attached and offset — the rigorous, load-bearing proof of
this property is `tests/world/`'s own hand-computed-matrix unit test
(V9), not runtime animation.

**Camera is static, matching Spec 0013's own D5 precedent exactly, for
the identical reason:** a fixed camera keeps the manual by-eye comparison
against the new golden (D10) meaningful frame-to-frame — an orbiting
camera would only match on whichever frame happens to align.

### D10. New headless image-regression fixture and golden — CMake wiring, ADR-0042 compliance

**New fixture, sibling to `MinimalCubeFixture`, same target
(`atlantis_image_regression_fixture`), same "duplicated, not shared"
precedent (D7's own citation applies identically here):**
`tests/image_regression/fixture/world_scene_fixture.h`/`.cpp` — a
`WorldSceneFixture` struct shaped exactly like `MinimalCubeFixture`
(`device`, `mesh`, `material`, `cameraBuffer`, `depthTexture`,
`offscreenTarget`, `readbackBuffer`) **plus** `atlantis::world::World
world` and `atlantis::asset_system::AssetId knownMinimalCubeAssetId`.
`setUpWorldSceneFixture(artifactPath, metadataPath)` mirrors
`setUpMinimalCubeFixtureFromAsset()` exactly for every GPU-resource
field (one `Mesh`, one `Material` — the fixture's own scene, like
Runtime's, reuses a single cooked mesh for every entity) and additionally
builds D9's own six-entity scene via the same `World` public API calls
Runtime uses (duplicated construction code, not a shared "build the
scene" function — matching this same section's own precedent).
`renderOneWorldSceneFrame(fixture)` mirrors `renderOneFrame()` exactly
through the acquire/copy/submit/`waitIdle()` sequence, with its own
camera-matrix-write and `DrawItem`-building steps replaced by the same
logic D8 describes for Runtime (`updateTransforms()`, camera extraction,
per-entity `resolveMeshAsset()`), duplicated independently — this
fixture links `Atlantis::World` newly (added to
`tests/image_regression/fixture/CMakeLists.txt`'s existing PUBLIC list,
alongside the already-present `Atlantis::AssetSystem`) but reuses its own
already-duplicated `lookAt()`/`perspective()`/`identityMatrix()` helpers
already private to `minimal_cube_fixture.cpp` — duplicated a *third*
time into `world_scene_fixture.cpp`'s own anonymous namespace, matching
the same established precedent exactly, not consolidated across the two
fixture files either.

**New GPU-required test case, same executable
(`atlantis_image_regression_gpu_tests`), new source file** —
`tests/image_regression/world_scene_gpu_tests.cpp` (added to the
existing `add_executable()` call's source list; `Atlantis::World` added
to that target's own `target_link_libraries()`), comparing the captured
multi-entity frame against a **new** golden at
`tests/image_regression/goldens/world_scene/world_scene_512x512_rgba8unorm.png`
using the existing, unmodified
`atlantis::image_regression::compareBuffers()`, requiring the same
**zero**-channel-difference bar the existing `minimal_cube` golden test
already requires. **The existing `minimal_cube` golden and its own test
case are untouched** — this is a new, additional `TEST_CASE`, not a
modification of `image_regression_gpu_tests.cpp`.

**A new, second, standalone golden-generator executable — not a change
to the existing `atlantis_image_regression_golden_generator` — matching
this Plan's own "duplicated, not shared" precedent one more time, for a
concrete, disclosed reason: zero regression risk to the already-working
`minimal_cube` golden path.** `tests/image_regression/golden_generator/world_scene_main.cpp`,
new target `atlantis_image_regression_world_scene_golden_generator` (both
declared in the existing `golden_generator/CMakeLists.txt`, alongside the
existing target — same file, two `add_executable()` calls), reusing the
identical `Atlantis::ImageRegressionSupport`-provided git/provenance/PNG
steps (1–4, 6–9 of the existing tool's own nine numbered steps) with only
step 5 ("render one frame") replaced to call
`setUpWorldSceneFixture()`/`renderOneWorldSceneFrame()` instead — this
duplicates roughly 60 of the existing tool's ~270 lines (the
generic git/provenance/PNG boilerplate), a deliberate, disclosed cost
matching every other duplication decision in this Plan, not an
oversight. **Never CTest-registered**, matching the existing tool's own
already-`Accepted` boundary exactly.

**ADR-0042 "Initial baseline bootstrap" compliance — the exact procedure
the Implementation PR must follow and record, not left implicit:**

1. **Applicability (constraint 1):** confirmed satisfiable by
   construction — `tests/image_regression/goldens/world_scene/` does not
   exist on `main` today (this Plan's own Files/Modules Touched list is
   the first thing that creates it); this category is therefore available
   and, per ADR-0042's own Alternatives Considered, is the **correct**
   category — not "Approved rebaseline," which requires old-vs-new diff
   evidence this first capture cannot produce.
2. **Source revision (constraint 2), commit ordering (constraint 4):**
   unchanged from the general rule — the code implementing this Plan (the
   `World` module, Runtime's own extraction, the new fixture/test/
   generator) is committed **first**, against a clean working tree; the
   golden PNG + sidecar are captured against that already-existing
   commit and added via a **separate, subsequent commit**.
3. **Full provenance (constraint 3):** the new sidecar carries the exact
   same field set the existing `minimal_cube` sidecar already does (see
   its own real file, read during this Plan's own Independent Review) —
   `schema_version`, `capture_date`, `source_revision`, all seven
   hardware/Vulkan provenance fields, `extent_width`/`extent_height`
   (`512`/`512`, matching `kFixtureExtentPixels`, unchanged), `format`
   (`Rgba8Unorm`, unchanged) — the existing `serializeGoldenProvenance()`
   is reused unmodified; no new sidecar schema is introduced.
4. **Substitute evidence (constraint 5), recorded in the Implementation
   PR, not merely asserted here:** (a) a screenshot or attached PNG of
   the captured frame, human-reviewed to confirm a correctly-rendered,
   non-degenerate scene (five distinct, correctly-shaded, depth-ordered
   cubes, one visibly offset from its own parent, framed by the camera);
   (b) the real capture-compare cycle run immediately after capture,
   confirming **zero** channel difference against the golden it just
   wrote (the comparison algorithm's own self-consistency check); (c) a
   real run on real Vulkan-capable GPU hardware with Validation Layers
   grepped clean; (d) citing [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)'s
   own already-recorded empirical calibration (the channel-tolerance-0/
   failing-pixel-budget-0 rule) as the basis for requiring zero
   difference here too — this Plan does not re-run that calibration
   (it was performed once, for the reference GPU/driver, and applies to
   any scene captured on that same reference environment, per ADR-0042's
   own Context).
5. **No relaxation of any other Decision-section rule (constraint 6):**
   the golden validity check, comparison algorithm, provenance-mismatch
   diagnostic, and golden-regeneration-tool boundary all apply to this
   new golden exactly as they already apply to `minimal_cube`'s —
   nothing in this Plan changes any of them.

### D11. `WorldError`'s four enumerators are exhaustive for this Plan's own scope

Confirmed by construction, not merely asserted: every setter/getter's own
only failure modes are "handle belongs to a different, live `World`"
(`WrongWorld`, checked first by `validate()`, D3), "handle stale or
out-of-range" (`InvalidEntity`), (for `setParent()`) "would create a
cycle," or (for `setActiveCamera()`) "no `Camera` component" — no fifth
condition exists anywhere in D3–D5's own algorithms. `WrongWorld` is
added by this Plan, per ADR-0049's own Proposed Amendment (Human Review
direction, 2026-08-22) — the only `WorldError` enumerator change from
the prior round.

## Milestones / Task Breakdown

Each step leaves the repository configuring, building, and (from Step 1
onward) its own tests passing.

**Atomicity policy (revised, Plan Review Round 2):** a step (or part of a
step) is marked **atomic** only where splitting it would actually break
`cmake` configure/build, or violate a stated provenance rule (ADR-0042)
— not merely because grouping several related pieces of work into one
step was convenient to describe. Round 1 marked three whole steps (1, 5,
7) atomic on reasoning that did not meet this bar on re-audit (each
bundled more functionality than the one genuine mechanical minimum
required); those labels are removed below, replaced with an explicit
statement of the narrower minimum, if any, that step actually requires.
Implementation may land the remainder of any non-atomic step's own
content as one commit or split it further, at its own discretion, as
long as every individual commit still configures and builds. Step 8
remains a **mandatory separate commit** from Step 7 — a distinct kind of
constraint (must not be *merged backward* into the preceding step, a
provenance rule) from atomicity (must not be split apart internally);
the two are not the same thing and are labeled differently below.

### Step 1 — `Atlantis::World` module skeleton, value types, slot map, entity lifecycle

The **only** genuine mechanical minimum: `src/world/CMakeLists.txt`'s own
`add_library(atlantis_world STATIC ...)` call and the root
`CMakeLists.txt`'s `add_subdirectory(src/world)` edit must land together
with **at least one** non-empty `.cpp` in that same commit — CMake
rejects configuring a STATIC library target with an empty source list,
and the root file's `add_subdirectory()` call requires the directory it
names to already exist and configure cleanly. Nothing else in this
step's own content below is mechanically required to land in the same
commit as that minimum; it is grouped here for a cohesive, reviewable
unit (a working entity-lifecycle slice with its own passing tests), not
because splitting it further would break anything. Implementation may
split entity lifecycle (`createEntity()`/`isValid()`), cascading destroy,
identity/`WrongWorld` checking, and `World`'s own special-member-function
shape (D3's copy/move decision, V22) into separate commits within this
step if it prefers.

- `src/world/CMakeLists.txt` — `atlantis_world` + `Atlantis::World` alias,
  D1's dependency list.
- `src/world/include/atlantis/world/{vec3,entity_id,world_error,transform,camera,renderable}.h`
  — D2, in full, including `entity_id.h`'s own `WorldIdentity` forward
  declaration and `EntityId`'s three-field shape, and `world_error.h`'s
  new `WrongWorld` enumerator.
- `src/world/include/atlantis/world/world.h` / `src/world/src/world.cpp`
  — `World`'s full public API (D4) and internal slot map (D3), including
  the private `WorldIdentity` definition, the out-of-line constructor/
  destructor, and the `validate()` helper, **except**
  `updateTransforms()`/`getWorldMatrix()` (Step 2) and
  `renderableEntities()` (needs `Renderable`, present from this step, but
  grouped into Step 3 alongside `Camera`/`Renderable` accessor tests for
  cohesion — the method itself compiles here regardless).
- `tests/world/CMakeLists.txt`,
  `tests/world/{entity_lifecycle_tests.cpp,hierarchy_tests.cpp,module_boundary_tests.cpp}`
  — V1–V4, V6 (partial — the atomicity and cycle-prevention halves that
  do not depend on `updateTransforms()`), V16, V22 (D3's copy/move
  `static_assert`s), and the new identity-token coverage: V23 (two
  simultaneously live `World` instances' own first entities, `{index=0,
  generation=0}` in both, correctly cross-reject with `Err(WrongWorld)`),
  V24 (`WrongWorld` reachable from every `EntityId`-accepting entry
  point, not only `isValid()`), V25 (an `EntityId` issued before a
  `World` move-construction remains valid — `Ok`, not `WrongWorld` or
  `InvalidEntity` — against the moved-to instance), all added to
  `entity_lifecycle_tests.cpp`. `module_boundary_tests.cpp` created here
  (not later), mirroring `tests/asset_system/`'s own precedent of
  creating the boundary scan as soon as the module has any source, so it
  covers every file this Plan adds in every later step automatically.
- Root `CMakeLists.txt` — `add_subdirectory(src/world)` and
  `add_subdirectory(tests/world)` (D1's ordering).

### Step 2 — Math contract and `updateTransforms()`

- `src/world/src/world.cpp` — the private matrix-math helpers (D6: layout,
  `multiply()`, per-axis rotation matrices, `composeLocal()`) and
  `updateTransforms()` (now fully iterative, no recursive helper) and
  `getWorldMatrix()` (D6).
- `tests/world/CMakeLists.txt` — `math_contract_tests.cpp` and
  `update_transforms_tests.cpp` added to `atlantis_world_tests`'s
  existing source list.
- `tests/world/{math_contract_tests.cpp,update_transforms_tests.cpp}` —
  V5, V7, V8, V9, V10.

### Step 3 — Camera and Renderable components, traversal, active-camera rule

- `src/world/src/world.cpp` — `setCamera()`/`removeCamera()`/
  `getCamera()`, `setActiveCamera()`/`clearActiveCamera()`/
  `activeCamera()`, `setRenderable()`/`removeRenderable()`/
  `getRenderable()`, `renderableEntities()` (D4).
- `tests/world/CMakeLists.txt` — `camera_tests.cpp`,
  `renderable_tests.cpp`, and `traversal_determinism_tests.cpp` added to
  `atlantis_world_tests`'s existing source list.
- `tests/world/{camera_tests.cpp,renderable_tests.cpp,traversal_determinism_tests.cpp}`
  — V11, V12, V13, V14.

### Step 4 — Runtime: `scene_extraction.h`/`.cpp`, new `RuntimeInitError` enumerators

- `src/runtime/include/atlantis/runtime/scene_extraction.h` /
  `src/runtime/src/scene_extraction.cpp` — D7, in full (moved out of
  `runtime_application.cpp`'s own anonymous namespace: `lookAt()` →
  `lookAtMatrix()`, `perspective()` → `perspectiveMatrix()`,
  `identityMatrix()` stays, all now non-anonymous-namespace members of
  `atlantis::runtime` so `tests/runtime/` can call them directly).
- `src/runtime/include/atlantis/runtime/init_error.h` /
  `src/runtime/src/init_error.cpp` — two new `RuntimeInitError`
  enumerators, `AssetMetadataParseFailed` and `SceneConstructionFailed`
  (D8), added to the existing enum and its existing `toString()`-style
  mapping function.
- `src/runtime/CMakeLists.txt` — `src/scene_extraction.cpp` added to
  `atlantis_runtime_host`'s existing `add_library()` source list;
  `Atlantis::World` added to that same target's existing PUBLIC
  dependency list (D1's own table is the authoritative statement of the
  target's full dependency set going forward).
- `tests/runtime/CMakeLists.txt` — `scene_extraction_tests.cpp` added to
  the existing `atlantis_runtime_tests` executable's own source list —
  no new CMake target.
- `tests/runtime/{scene_extraction_tests.cpp,init_error_tests.cpp (extended)}`
  — V15 (camera-matrix extraction, all four cases: success, both
  degenerate inputs, the shear-robustness case reproducing V8's own
  counter-example world matrix directly, not merely a similar one), the
  `resolveMeshAsset()` match/no-match cases, and the two new
  `RuntimeInitError` enumerators' own
  `toString()`/exit-code mapping.

### Step 5 — `RuntimeApplication`: new members, scene construction, extraction-driven `runFrame()`

No genuine build/configure or provenance constraint applies here: a
`RuntimeApplication` with D8's new members added but `runFrame()`'s Step
7 not yet replaced still compiles and links cleanly (unused members are
harmless), and Round 1's own "would silently under-deliver the multiple-
`DrawItem` requirement" reasoning describes an *incomplete feature*, not
a build failure — the ordinary, expected state of any in-progress,
multi-commit change. Implementation may land D8's new members,
`initializeSteps()`'s new scene-construction step, and `runFrame()`'s
replaced Step 7 as one commit or split them further, at its own
discretion.

- `src/runtime/include/atlantis/runtime/runtime_application.h` — D8's new
  members.
- `src/runtime/src/runtime_application.cpp` — D6's metadata-for-`AssetId`
  read (new `initializeSteps()` step), D9's scene construction (new
  `initializeSteps()` step), D8's replaced `runFrame()` Step 7 (now
  calling `scene_extraction.h`'s own functions instead of file-local
  `lookAt()`/`perspective()`, which this step removes from this file's
  own anonymous namespace — Step 4 already moved them to
  `scene_extraction.cpp`).
- No new automated test in this step, matching Plan 0013's own Step 3
  precedent exactly: `atlantis_runtime_host` must still **compile**
  cleanly against `Atlantis::World`, a real, meaningful check on its
  own; this step's own new orchestration code is exercised for the first
  time by Step 6's GPU smoke test and verified in full by Step 8.

### Step 6 — Runtime GPU smoke test extension

- `tests/runtime/runtime_smoke_gpu_tests.cpp` — the existing `TEST_CASE`
  is unchanged in its own bounded-loop structure (D10 of Plan 0013,
  untouched); its own assertions are extended to additionally confirm
  the acquired frame's `DrawItem` count reaching `Renderer::drawFrame()`
  is `5` (every validation-scene `Renderable` entity resolved) on a
  successful run — the first `gpu`-labeled confirmation that this
  Plan's own multi-item span actually reaches `Renderer::drawFrame()`
  with more than one item, matching Spec 0014's own central,
  now-implemented claim.

### Step 7 — New headless fixture, golden generator, GPU test case

No genuine build/configure constraint applies: a `.cpp`/`.h` pair added
under `tests/image_regression/fixture/` but not yet referenced by
`CMakeLists.txt`'s own `add_library()` source list simply is not
compiled — CMake silently ignores an unreferenced file rather than
failing configure or build, so a header/source-first, CMake-wiring-second
split (or any other split) does not break anything. Grouped here as one
cohesive, reviewable unit; Implementation may split the fixture code, its
CMake wiring, the new GPU test case, and the new golden-generator
executable into separate commits if it prefers.

- `tests/image_regression/fixture/world_scene_fixture.h` / `.cpp` — D10.
- `tests/image_regression/fixture/CMakeLists.txt` — `Atlantis::World`
  added to `atlantis_image_regression_fixture`'s PUBLIC link list; the
  new `.cpp` added to its own `add_library()` source list.
- `tests/image_regression/world_scene_gpu_tests.cpp` — D10's new
  `TEST_CASE`, comparing against the not-yet-existing golden (this test
  is expected to fail — `INVALID GOLDEN`, per ADR-0042's own validity
  check — until Step 8 captures it; this is disclosed, not a defect in
  this step's own commit).
- `tests/image_regression/CMakeLists.txt` — the new source file added to
  `atlantis_image_regression_gpu_tests`'s existing source list;
  `Atlantis::World` added to that target's own link libraries.
- `tests/image_regression/golden_generator/world_scene_main.cpp` — D10.
- `tests/image_regression/golden_generator/CMakeLists.txt` — the new
  `add_executable()` call (D10), reusing the existing
  `Atlantis::ImageRegressionSupport`/`Atlantis::ImageRegressionFixture`
  link libraries, never CTest-registered.

### Step 8 — Golden capture (**must be its own separate, subsequent commit — a provenance rule, not an atomicity rule; never folded backward into Step 7's own commit(s), per D10's own ADR-0042 procedure**)

- Run `atlantis_image_regression_world_scene_golden_generator world_scene/world_scene_512x512_rgba8unorm`
  against a clean working tree, on real Vulkan-capable hardware.
- `tests/image_regression/goldens/world_scene/{world_scene_512x512_rgba8unorm.png,world_scene_512x512_rgba8unorm.sidecar.txt}`
  — the tool's own output, committed as-is.
- Re-run `atlantis_image_regression_world_scene_gpu_tests` (now passing,
  zero channel difference against the golden just captured) — D10's own
  constraint-5(b) self-consistency evidence.

### Step 9 — Full verification (Debug/Release, GPU-independent, GPU-required, Validation Layers, existing regression, manual)

- Clean Debug and Release configure + build.
- `ctest -LE gpu` and `ctest -L gpu`, both configurations — `-LE gpu`
  confirms V1–V16 (`atlantis_world_tests`, the extended
  `atlantis_runtime_tests`); `-L gpu` confirms V17 (`runtime_smoke_gpu_tests`,
  now requiring the extended `atlantis_runtime`) and V18
  (`world_scene_gpu_tests`, alongside the existing, unmodified
  `image_regression_gpu_tests`); together, on both configurations, this
  is V19.
- Vulkan Validation Layers grepped clean on every GPU-touching path, both
  configurations (part of V19).
- Manual windowed verification of the real `atlantis_runtime` executable
  (not the smoke test): a visible window shows five distinct cube
  instances at their D9-specified relative positions (including D
  visibly offset from and attached to C), correctly shaded and depth-
  ordered, matching the newly-captured golden by eye; interactive
  resize, minimize/restore, and a normal close all behave exactly as
  Spec 0013's own already-verified bar requires, unchanged by this Plan
  — V20.
- `git diff --stat` confirms no file under `src/rhi/`, `src/renderer/`,
  `src/render_graph/`, `src/vulkan_backend/`, `src/platform/`,
  `src/shader_system/`, `src/asset_system/`, `shaders/`, or the existing
  `tests/image_regression/goldens/minimal_cube/` golden was modified —
  V21.

### Step 10 — Documentation and registry closeout

- `AGENTS.md` — the Module boundaries section gains an `Atlantis::World`
  paragraph (exact wording an Implementation-time detail, matching every
  prior Plan's own precedent of fixing *what* changes, not final prose),
  and the Runtime paragraph's own dependency list gains `Atlantis::World`.
- `docs/architecture/module_boundaries.md` — a new `## Atlantis World`
  section, matching the existing per-module format; the `## Atlantis
  Runtime` section's own `Depends on` line gains `Atlantis::World`.
- `docs/project-blueprint.md` — a new Milestone entry for World / Scene
  Foundation, matching the existing per-milestone format.
- `src/README.md` — add a `world/` entry, matching the existing
  per-module paragraph format.
- `specs/README.md` — Spec 0014's own Implementation column updated from
  "Not started" to reference the Implementation PR by number once opened
  (matching Spec 0012/0013's own established two-stage "OPEN, not yet
  merged" → "Implemented and merged" convention). This Plan-drafting
  round's own registry edit (the Plan column, and the "In Review" Plan
  status) is separate from, and precedes, that future edit — see Files/
  Modules Touched.

## Files / Modules Touched (expected)

**New — World module**

- `src/world/CMakeLists.txt`
- `src/world/include/atlantis/world/{vec3,entity_id,world_error,transform,camera,renderable,world}.h`
- `src/world/src/world.cpp`

**New — Runtime extension**

- `src/runtime/include/atlantis/runtime/scene_extraction.h`
- `src/runtime/src/scene_extraction.cpp`

**New — tests**

- `tests/world/{CMakeLists.txt,entity_lifecycle_tests.cpp,hierarchy_tests.cpp,math_contract_tests.cpp,update_transforms_tests.cpp,camera_tests.cpp,renderable_tests.cpp,traversal_determinism_tests.cpp,module_boundary_tests.cpp}`
- `tests/runtime/scene_extraction_tests.cpp`
- `tests/image_regression/fixture/world_scene_fixture.h`
- `tests/image_regression/fixture/world_scene_fixture.cpp`
- `tests/image_regression/world_scene_gpu_tests.cpp`
- `tests/image_regression/golden_generator/world_scene_main.cpp`

**New — golden**

- `tests/image_regression/goldens/world_scene/{world_scene_512x512_rgba8unorm.png,world_scene_512x512_rgba8unorm.sidecar.txt}`
  (Step 8's own separate, subsequent commit)

**Modified**

- `CMakeLists.txt` (root) — two `add_subdirectory()` lines (`src/world`,
  `tests/world`).
- `src/runtime/CMakeLists.txt` — `Atlantis::World` added to
  `atlantis_runtime_host`'s link libraries; `src/scene_extraction.cpp`
  added to that same target's own `add_library()` source list (Step 4).
- `src/runtime/include/atlantis/runtime/runtime_application.h` — D8's
  new members.
- `src/runtime/src/runtime_application.cpp` — D6/D8/D9's new
  initialization steps and replaced `runFrame()` Step 7; the file-local
  `lookAt()`/`perspective()` helpers removed (now in
  `scene_extraction.cpp`).
- `src/runtime/include/atlantis/runtime/init_error.h`,
  `src/runtime/src/init_error.cpp` — two new enumerators.
- `tests/runtime/CMakeLists.txt` — `scene_extraction_tests.cpp` added to
  the existing `atlantis_runtime_tests` executable's source list (Step
  4) — the same GPU-independent target `lifecycle_state_tests.cpp`/
  `exit_reason_tests.cpp`/`error_classification_tests.cpp`/
  `init_error_tests.cpp` already join, not a new executable.
- `tests/runtime/runtime_smoke_gpu_tests.cpp` — extended assertion (Step
  6), no structural change.
- `tests/runtime/init_error_tests.cpp` — extended for the two new
  enumerators.
- `tests/image_regression/fixture/CMakeLists.txt` — additive
  (`Atlantis::World`, new source file).
- `tests/image_regression/CMakeLists.txt` — additive (new source file,
  `Atlantis::World`).
- `tests/image_regression/golden_generator/CMakeLists.txt` — additive
  (new `add_executable()`).
- `AGENTS.md`, `docs/architecture/module_boundaries.md`,
  `docs/project-blueprint.md`, `src/README.md`, `specs/README.md` (Step
  10).
- `specs/0014-world-scene-foundation.md` — `Related Plan(s)` field
  updated to link this Plan (this Plan-drafting round, not
  Implementation).
- `specs/README.md` — Spec 0014's own Plan column updated (this Plan-
  drafting round); the Implementation-column update described under
  Step 10 is future Implementation-PR work.

**Explicitly not touched:** `src/rhi/`, `src/renderer/`,
`src/render_graph/`, `src/vulkan_backend/`, `src/platform/`,
`src/shader_system/`, `src/asset_system/`, `src/tools/`, `assets/`,
`shaders/`, `examples/`, `tests/asset_system/`, `tests/shader_system/`,
`tests/rhi/`, `tests/vulkan_backend/`, `tests/render_graph/`,
`tests/renderer/`, `tests/platform/`, `tests/core/`,
`tests/tools/`, `cmake/`, every existing example/demo, the existing
`minimal_cube` golden and its own test case, and every existing
`Accepted` ADR.

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
  `resolveMeshAsset()` cases use `atlantis::asset_system::AssetId`
  values consistent with what `Renderable` (Step 1) already carries, and
  because Step 4's own camera-matrix tests reuse D9's counter-example
  transform values, which are only meaningful once `Camera`/`Transform`
  (Steps 1–3) are complete.
- Step 5 needs Step 4 for `scene_extraction.h`'s own functions to exist.
- Step 7 needs Step 3 (a complete `World` public API) but is otherwise
  independent of Steps 4–6 — the new fixture duplicates its own
  extraction logic rather than depending on `src/runtime/` in any way
  (the two branches in the diagram above may be implemented and reviewed
  in either order, or in parallel).
- Step 8 needs Step 7's own executable to exist to run it, and must be a
  **separate commit** from Step 7 (D10's own ADR-0042 procedure) even
  though it depends on Step 7 having already landed.
- Step 9 needs both Step 6 and Step 8 — it exercises the windowed smoke
  test (Step 6) and the headless golden (Step 8) together.

## Verification Checklist

| # | Verification | Where | Kind |
|---|---|---|---|
| V1 | `createEntity()` always succeeds, returns a valid handle; a fresh `World` has no live entities. | `entity_lifecycle_tests.cpp` | GPU-independent |
| V2 | `destroyEntity()`: invalidates the target and, recursively, every transitive descendant, in one call; every subsequent operation against any of those handles returns `Err(InvalidEntity)`; an unrelated sibling/ancestor is untouched. An out-of-range `index` (beyond any slot ever allocated) is rejected the same way. Same-`World`-instance behavior only — cross-instance behavior is V23–V25. | `entity_lifecycle_tests.cpp` | GPU-independent |
| V3 | Slot reuse: destroying and recreating produces a **different** generation at the same index; the old `EntityId` is correctly `Err(InvalidEntity)`; the free list's own LIFO order is directly observed (destroy A then B; the next two `createEntity()` calls reuse B's index first, then A's). | `entity_lifecycle_tests.cpp` | GPU-independent |
| V4 | **Generation retirement, at the real boundary, not merely a design-time argument:** using the test-only friend access D3 describes, a slot's generation is set to `max() - 1`, `destroyEntity()`d, and confirmed (a) its generation is now the tombstone value; (b) a following `createEntity()`, or a sequence exhausting every other free slot, never reuses that index; (c) an `EntityId` at that index with its old, pre-retirement generation still correctly returns `Err(InvalidEntity)`, via the same, unmodified check every other stale-handle case already uses. | `entity_lifecycle_tests.cpp` | GPU-independent |
| V5 | `setParent()`: succeeds for a valid, non-cycle-forming request; rejects a direct self-parent, a two-hop cycle, and a four-hop transitive cycle, each with `Err(WouldCreateCycle)`, leaving the hierarchy **completely unchanged** (re-read via `getParent()` on every entity involved, not just the one call's own target); a stale `child`/`parent` handle is `Err(InvalidEntity)`, checked before any cycle walk. | `hierarchy_tests.cpp` | GPU-independent |
| V6 | Every mutating call's own `Err` path (`setParent()` cycle/invalid-handle; `destroyEntity()`/`setLocalTransform()`/`setCamera()`/`setRenderable()`/etc. on an invalid handle) leaves every observable `World` state — parent links, local transforms, component presence, `isValid()` for every other entity — byte-identical to immediately before the call. | `hierarchy_tests.cpp`, `entity_lifecycle_tests.cpp` | GPU-independent |
| V7 | `updateTransforms()`/`getWorldMatrix()`: a multi-level chain (root → child → grandchild), each with a distinct `Transform`, produces world matrices matching an independently hand-computed expected result to floating-point tolerance — verifying column-major layout, `parentWorld · local` composition, `T · R · S` order, and the fixed `Ry · Rx · Rz` Euler order together, not merely "does it run." A root entity's world matrix equals its own local matrix. | `math_contract_tests.cpp` | GPU-independent |
| V8 | **Shear under a scaled hierarchy:** a parent with `localScale = (2,1,1)` composed with a child rotated 45° about `Z` (ADR-0050's own counter-example, reproduced exactly) produces a world matrix whose own linear-part columns 0/1 have a non-zero dot product, matching the hand-computed `−1.5` value ADR-0050 itself records — confirming `updateTransforms()` does not attempt to "correct" or reject shear. | `math_contract_tests.cpp` | GPU-independent |
| V9 | Moving/rotating a parent (`setLocalTransform()` on the parent, then `updateTransforms()`) changes a child's own `getWorldMatrix()` result in exactly the way composing the new parent matrix with the child's unchanged local matrix predicts — this is the rigorous proof behind Spec 0014's own "moving a parent moves its child" property (D9), exercised here, not only via the static windowed demo. | `update_transforms_tests.cpp` | GPU-independent |
| V10 | `setParent()` preserves the child's own `getLocalTransform()` byte-for-byte across a reparent; its `getWorldMatrix()` (after `updateTransforms()`) changes when, and only when, the old and new parent's own world matrices actually differ. | `update_transforms_tests.cpp` | GPU-independent |
| V11 | `Camera`: `setCamera()`/`getCamera()`/`removeCamera()` round-trip and correctly report `Err(InvalidEntity)` on a stale handle; `setActiveCamera()` fails `Err(NoCameraComponent)` against an entity with no `Camera`; destroying the active camera entity (directly, or transitively via cascading destroy of an ancestor) clears `activeCamera()` to `std::nullopt` automatically; a fresh `World`'s own `activeCamera()` starts `std::nullopt`. | `camera_tests.cpp` | GPU-independent |
| V12 | `Renderable`: `setRenderable()`/`getRenderable()`/`removeRenderable()` round-trip, by value (confirmed: mutating the caller's own local `Renderable` copy after passing it to `setRenderable()` does not affect what `getRenderable()` later returns). | `renderable_tests.cpp` | GPU-independent |
| V13 | Every public getter (`getLocalTransform()`, `getWorldMatrix()`, `getCamera()`, `getRenderable()`) returns a plain value, never a reference/pointer — confirmed at compile time via `static_assert` on each method's own return type, matching `runtime_ownership_tests.cpp`'s own established `static_assert`-based pattern. | `entity_lifecycle_tests.cpp` (or a small dedicated `by_value_access_tests.cpp`) | GPU-independent (compile-time) |
| V14 | **Deterministic enumeration:** a fixed sequence of `createEntity()`/`destroyEntity()` calls exercising the LIFO free list produces the exact same `renderableEntities()` ascending-slot-index ordering across repeated, independent runs of the same test. | `traversal_determinism_tests.cpp` | GPU-independent |
| V15 | `extractCameraMatrices()`: a well-formed input (including the V8 shear-producing configuration, confirming the `eye`/`forward`-only extraction stays robust) produces an orthonormal `view` basis (row/column dot products zero, unit length) and a correct `projection`; a near-zero-length forward returns `Err(DegenerateCameraForward)`; a forward parallel to `(0,1,0)` returns `Err(DegenerateCameraBasis)`; a negatively-scaled ancestor's world matrix still produces a proper (determinant `+1`, non-reflected) `view` basis. `resolveMeshAsset()`: matching and non-matching `AssetId` pairs each return the correct `Ok`/`Err(UnresolvedMeshAsset)`. | `scene_extraction_tests.cpp` | GPU-independent |
| V16 | No source under `src/world/` includes `atlantis/rhi/`, `atlantis/renderer/`, `atlantis/render_graph/`, `atlantis/shader_system/`, `atlantis/platform/`, `atlantis/vulkan_backend/`, `atlantis/runtime/`, or any `vulkan` header — a directory-scanning test mirroring `tests/asset_system/module_boundary_tests.cpp`'s own already-`Accepted` pattern. | `module_boundary_tests.cpp` | GPU-independent |
| V17 | Real GPU, windowed: `atlantis_runtime_gpu_tests`' extended assertion confirms exactly 5 `DrawItem`s reach `Renderer::drawFrame()` on a successful frame; Vulkan Validation Layers report zero warnings/errors for the full multi-item span (the smoke test's own existing crash-on-validation-hit mechanism, unchanged). | `runtime_smoke_gpu_tests.cpp` | `gpu`-labeled |
| V18 | Real GPU, headless: the new `world_scene` golden compare achieves **zero** channel difference; the `minimal_cube` golden's own existing test still passes, unmodified, proving the new fixture's own construction did not disturb the existing one (they are independent library sources in the same target, but this is verified empirically, not merely asserted from the file list). | `world_scene_gpu_tests.cpp`; `image_regression_gpu_tests.cpp` (existing, re-run) | `gpu`-labeled |
| V19 | Debug **and** Release: clean configure + build; `ctest -LE gpu` and `ctest -L gpu` both green on both configurations; Vulkan Validation Layers grepped clean (not merely inferred from exit status) on every GPU-touching path. | Both configurations | Manual, recorded |
| V20 | Manual windowed: the real `atlantis_runtime` executable shows five distinct, correctly-shaded, depth-ordered cubes at their D9 positions (D visibly offset from C), matching the new golden by eye; interactive resize/minimize/restore/close all behave per Spec 0013's own already-established bar, unchanged. **This is visual confirmation only — it is not, and does not substitute for, the mathematical proof of Camera scale/degenerate-input correctness, which V15 alone provides.** | Manual | Manual |
| V21 | `git diff --stat` confirms no file under `src/rhi/`, `src/renderer/`, `src/render_graph/`, `src/vulkan_backend/`, `src/platform/`, `src/shader_system/`, `src/asset_system/`, `shaders/`, or `tests/image_regression/goldens/minimal_cube/` was modified, and no existing example/demo/CMake target/test was removed or renamed. | Manual, recorded in the PR | Manual |
| V22 | `World` is move-constructible, and **not** copy-constructible, copy-assignable, or move-assignable — confirmed via `static_assert` on each trait, matching `runtime_ownership_tests.cpp`'s own established pattern. | `tests/world/` (a small dedicated test file, or added to `entity_lifecycle_tests.cpp`) | GPU-independent (compile-time) |
| V23 | **Two simultaneously live `World` instances, first-entity collision:** construct `World a; World b;`, call `a.createEntity()` and `b.createEntity()` (both correctly `{index=0, generation=0}`, differing only in `worldIdentity`); confirm `b`'s every `EntityId`-accepting API rejects `a`'s handle with `Err(WorldError::WrongWorld)` (not `Err(InvalidEntity)`, not coincidental `Ok`), and symmetrically for `a` rejecting `b`'s handle — the concrete case ADR-0049's own Proposed Amendment names as the common, not rare, hazard this design closes. | `entity_lifecycle_tests.cpp` | GPU-independent |
| V24 | **`WrongWorld` reachable from every `EntityId`-accepting entry point**, not only `isValid()`/`destroyEntity()`: `setParent()`/`getParent()`, `setLocalTransform()`/`getLocalTransform()`, `getWorldMatrix()`, `setCamera()`/`removeCamera()`/`getCamera()`, `setActiveCamera()`, `setRenderable()`/`removeRenderable()`/`getRenderable()` each correctly return `Err(WorldError::WrongWorld)` when passed a handle from a different, live `World` instance, checked **before** any index/generation-dependent behavior (e.g. `setParent()` never reaches its own cycle walk for a wrong-world handle). `kInvalidEntityId` itself is confirmed to still report `Err(InvalidEntity)`, never `WrongWorld`, against every one of these (the `worldIdentity == nullptr` carve-out). | `entity_lifecycle_tests.cpp`, `hierarchy_tests.cpp` | GPU-independent |
| V25 | **`EntityId` validity survives `World` move-construction:** create several entities (including a parent/child pair) in a `World`, capture their `EntityId`s, move-construct a new `World` from it (`World moved = std::move(original);`), and confirm every previously issued `EntityId` is still `Ok`/`isValid()` against `moved` — same index, same generation, same `worldIdentity` (the token's own heap address, unchanged by the move) — and that hierarchy/component data moved with it intact. | `entity_lifecycle_tests.cpp` | GPU-independent |

## Traceability — Spec / ADR → Plan

| Source requirement | Where satisfied |
|---|---|
| Spec 0014 — New top-level `Atlantis::World`, Core+AssetSystem(narrow) only | D1; V16 |
| Spec 0014 — `EntityId` index+64-bit-generation, by-value access, atomic mutation | D2, D3, D4; V1, V6, V13 |
| Spec 0014 — Overflow formally closed via slot retirement | D3; V4 |
| Spec 0014 — Stale-handle `Result` classification | D4; V2 |
| `EntityId` gains a `World`-identity field; `World` owns a heap-allocated, address-stable identity token — **pending ADR-0049/Spec 0014 Proposed Amendments reaching `Accepted`** | D2, D3, D4, D5; V22–V25; ADR-0049/Spec 0014 "Proposed Amendment" sections; Deviations |
| `World`'s own copy/move semantics, now load-bearing for identity | D3; V22, V25 |
| `WorldError::WrongWorld` — new fourth enumerator, checked before slot/generation at every entry point | D2, D3, D4, D5, D11; V23, V24 |
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
| Spec 0014 — No existing public rendering API changed | Files/Modules Touched's own "Explicitly not touched" list; V21 |
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
behavior with no migration step. Step 8's own golden-capture commit
(D10's own ADR-0042-required separate commit) reverts independently and
trivially (delete the two new golden files); its own preceding commit
(Step 7) remains fully buildable without it — `world_scene_gpu_tests.cpp`
simply reports `INVALID GOLDEN` again, exactly as it did before Step 8,
not a build failure.

## Deviations, objections, and open mechanical details

**No `Accepted` decision in Spec 0014 or ADR-0048–0051 was found to be
wrong or unimplementable while drafting this Plan** — every one proved
implementable against the real, current source tree exactly as written;
no accepted boundary needed relaxing, and no public API of any existing
module needed changing. One genuine gap the `Accepted` text left
*unaddressed* (not wrong, merely silent) is escalated as blocking below,
not silently resolved — see "BLOCKING" further down. One genuine
implementability question the Spec/ADRs left open — how Runtime obtains
`minimal_cube`'s real `AssetId` without a new
Asset System dependency edge — is resolved in D6 using two already-
`Accepted`, already-public Asset System functions
(`parseAssetMetadata()`, already returning an `AssetId`-carrying struct)
neither the Spec nor the ADRs named explicitly, found by reading Asset
System's own real public headers rather than assumed.

**Three duplication decisions, each disclosed with its own concrete
reason, not silently accumulated:** (1) `scene_extraction.h`'s own
camera-math functions are new, factored-out **Runtime-private** code, not
shared with the new image-regression fixture, which duplicates the same
logic independently — matching ADR-0051's own explicit rejection of a
shared "Extraction module," and this codebase's own long-standing
"duplicated, not shared" precedent for exactly this class of small
camera-math helper (already duplicated three times over across
`examples/minimal_renderer_demo`, `tests/image_regression/fixture/minimal_cube_fixture.cpp`,
and `src/runtime/`, before this Plan's own fourth copy). (2) The new
golden generator is a **second, standalone** executable, not a change to
the existing one — a deliberate, small (~60 line) boilerplate
duplication cost, accepted specifically to keep zero regression risk to
the already-verified `minimal_cube` golden path. (3) D9's validation-
scene construction code is written once for Runtime and once,
independently, for the headless fixture — not factored into a shared
"build the scene" function — for the identical reason as (1).

**Two genuinely open mechanical details, appropriately left to
Implementation, neither architectural:**

1. **The exact numeric camera-framing values in D9** are a reasoned
   starting point (derived from a straightforward pitch-angle
   calculation, not empirically rendered and inspected) — if the actual
   rendered frame does not comfortably frame all five cubes once first
   rendered, Implementation may adjust entity F's own position/rotation
   (and, if needed, the other five entities' own positions) to improve
   framing, **before** Step 8's golden capture — this is a numeric
   tuning pass within an already-fixed scene *structure* (six entities,
   one hierarchy relationship, one active camera), not a new
   architectural decision, and does not require revisiting D9's own
   table format or this Plan's own Human Review.
2. **The test-only friend-access mechanism V4 needs** (D3's own
   generation-retirement boundary test) is left as "a documented,
   narrowly-scoped `friend` declaration naming the one test translation
   unit that needs it" — the exact class/function name is an
   Implementation-time detail with no design content of its own (the
   *capability* — direct, test-only generation mutation — is fixed here;
   its C++ spelling is not).

**BLOCKING (updated, Round 3): cross-`World`-instance `EntityId` use has
a concrete design resolution, directed by Human Review — but the
governing amendments remain `Proposed`, not `Accepted`, so this Plan
still cannot proceed to its own Human Review Approval.** Round 1 of this
Plan's own review found the gap and reasoned it away as "not reachable by
any code this Plan writes, since `RuntimeApplication` constructs exactly
one `World` instance" — rejected in Round 2 as insufficient justification
for a public module's own correctness. Round 2 escalated the question to
Human Review with two options (document as UB, or add per-instance
identity). **Human Review has since responded (2026-08-22): Option A
(UB) is rejected; Option B is directed, specifically as a stable,
heap-allocated, address-stable per-`World` identity token — explicitly
not a global instance counter, and explicitly not `shared_ptr`/
`weak_ptr`-based identity, and explicitly not a restriction to a single
`World` instance per process.** This round (Round 3) applies that
direction throughout D2–D5/D11 and the Verification Checklist (V22–V25):
`EntityId` gains a `worldIdentity` field, `World` owns a
`std::unique_ptr<WorldIdentity>` token allocated once at construction,
every `EntityId`-accepting method validates identity before slot/
generation via a shared `validate()` helper, and a handle used against a
different, live `World` instance is reliably rejected with the new
`WorldError::WrongWorld` — not coincidental validation, and not silent
misuse of the wrong entity.

**Proposed Amendments recording this design, in full, have been appended
to both [ADR-0049](../adr/0049-entity-identity-and-handle-invalidation.md)
and [Spec 0014](../specs/0014-world-scene-foundation.md)** — both still
`Proposed`, deliberately: a direction given in review is not the same as
a recorded Human Review Approval, and this Plan does not write either
amendment as `Accepted` on its own authority. **This Plan's own Human
Review Approval, and Implementation, remain blocked until both amendments
are explicitly `Accepted`** — see Status above. Unlike Round 2's own
disposition, this is no longer an open design *question* pending a
choice between architecturally different options; it is a fully specified
design pending a formal Approval record, which this Plan is otherwise
ready for once that record exists.

**One disclosed, deliberately unmitigated edge case, distinct from the
generation-retirement risk ADR-0049 already closes:** `EntityId::index`
reaching its own `std::uint32_t` maximum (a `World` instance holding
over 4 billion **simultaneously alive** slots) is not mitigated by this
Plan, unlike per-slot generation exhaustion. Reaching it would require
memory this Spec's own validation scale never approaches (each `Slot` is
well over 40 bytes; 4 billion of them exceeds 160 GB) — a fundamentally
different, memory-bounded impossibility, not a per-slot churn count a
long-running process could plausibly reach, which is why ADR-0049 itself
never asked for an index-side mitigation and this Plan does not invent
one.

## Definition of Done

See [docs/process/definition-of-done.md](../docs/process/definition-of-done.md).
Deltas specific to this plan:

- V1–V18 and V22–V25 all executed and recorded; V19–V21 recorded as
  manual verification in the Implementation PR.
- Both the ADR-0049 and Spec 0014 Proposed Amendments (stable `World`
  identity token) have reached an explicit Human Review `Accepted` record
  before this Plan's own Human Review Approval — Implementation must not
  begin while either remains `Proposed`.
- The existing `minimal_cube` golden under
  `tests/image_regression/goldens/` is confirmed **unchanged** in the
  final diff; the new `world_scene` golden is captured per D10's own
  exact ADR-0042 "Initial baseline bootstrap" procedure, in its own
  separate commit (Step 8).
- `git diff --stat` confirms no file under `src/rhi/`, `src/renderer/`,
  `src/render_graph/`, `src/vulkan_backend/`, `src/platform/`, or
  `src/shader_system/` was modified.
- No new third-party dependency appears in `cmake/` or any
  `CMakeLists.txt`.
