# ADR 0111: Schema-Generated Typed Bindings

- **Status:** Accepted
- **Date:** 2026-10-08 (accepted 2026-10-09)
- **Deciders:** slmao
- **Acceptance:** slmao, 2026-10-09 (review of this branch's own PR,
  [PR #234](https://github.com/slmao/Atlantis/pull/234); accepted together with Spec 0057's Approval, its ten open
  questions ruled as recommended after review rounds 1 and 2; ADR-0099 D4,
  ADR-0105, ADR-0106, ADR-0103, ADR-0104 and ADR-0004 unchanged)
- **Related Spec:** [Spec 0057: Gameplay SDK](../specs/0057-gameplay-sdk.md) (`Approved`)
- **Related ADR(s):**
  - The bindings are generated **from**
    [ADR-0099](0099-engine-schema-core-and-descriptor-vocabulary.md)'s
    hand-authored descriptor tables. D4 ("no macros, no codegen" for the
    tables) stays in force: nothing here generates a descriptor.
  - This ADR settles, for C++,
    [ADR-0034](0034-stable-public-boundary-versus-internal-cpp-layout.md)'s
    open item "whether/how a future SDK is generated from schema versus
    hand-written".
  - It uses the schema that [ADR-0105](0105-runtime-connection-and-cli-client.md)'s
    `RuntimeConnection::schema()` serves, which
    [ADR-0106](0106-attachable-runtime-transport-and-control.md)'s
    transport carries with versions.
  - It pairs with [ADR-0110](0110-gameplay-sdk-client-library-and-execution-model.md)
    (the SDK the bindings belong to).

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

- **The maintainer asked for a generated typed interface** from the same
  schema as the reflective one (Spec 0057).
- **The schema is runtime data in C++:**
  - `worldSchema()` returns a span over a `constexpr` table defined in
    World's `.cpp`;
  - `TypeId`/`FieldId` are `constexpr` FNV-1a-64 of names (Core,
    `schema.h`);
  - every type has a `schemaVersion`;
  - `FieldDescriptor` carries kinds, flags and a `byteOffset` into World's
    C++ struct, which is internal layout under ADR-0034.
- **The repository has no generated source today.** Host tools (shader
  compiler, asset cooker, glTF importer) run at build time and are gated
  `if(NOT ANDROID)`. Android packages host-cooked *data* (ADR-0080), never
  host-generated *code*.
- **A client can be built against a different Runtime.** Over Remote, the
  schema arrives at connect with ids, kinds, flags and versions. Nothing
  compares it with what the client was compiled against.

## Decision

1. **A host generator writes the bindings; the output is committed** (Spec 0057 ruling Q3).
   - `atlantis_sdk_codegen` (`src/tools/sdk_codegen/`, host-only, gated
     like the other tools) links World for `worldSchema()`. It writes one
     C++ header:
     `src/gameplay_sdk/include/atlantis/gameplay/generated/world.h`.
   - The header is committed and reviewed like source. It starts with a
     "generated — do not edit" banner naming the generator and the
     regeneration command. It has no timestamp.
   - The generator is deterministic: the same table gives byte-identical
     output, with LF line endings.
   - **A staleness test** regenerates in memory and compares with the
     committed file. Any difference fails, whether from a schema edit not
     regenerated or a hand edit.
   - The SDK library has no build-time generation step, so it builds on
     Android unchanged.
2. **The generated shape** (Spec 0057 ruling Q4).
   - **Values:**
     - one plain value struct per schema struct, nested structs as
       members of their generated type;
     - one `enum class` per schema enum, its enumerators carrying the
       constants' exact values (never ordinals). Each enum's binding
       records whether 0 is a declared constant;
     - field types exactly the `PropertyValue` alternatives
       (`std::uint64_t`, `float`, `std::array<float, 3>`,
       `std::array<float, 4>`, `AssetGuid`, `EntityGuid`);
     - `Optional` → `std::optional<T>`;
     - members are value-initialized, not set to World's defaults (the
       schema carries no defaults, ADR-0099 D5). An enum member is
       therefore underlying value 0. That is whichever constant has the
       value 0, or no declared constant when none does; it is not "the
       first enumerator".
   - **Handles:** per component, one `constexpr` object in a `fields`
     namespace whose members mirror the canonical path
     (`world::fields::Light.intensity`,
     `world::fields::Camera.fog.density`).
     - Each handle carries the component's `TypeId`, the leaf's `FieldId`
       (Spec 0049 J8 addressing) and its C++ type.
     - A leaf without `Editable` gets a read-only handle type, so a set on
       it does not compile.
   - **Bindings:** per generated type, a `constexpr` description of what
     it was generated from:
     - qualified name, `TypeId` and `schemaVersion`;
     - each field's name, `FieldId`, kind, primitive kind, referenced
       `TypeId`, and `Optional` and `Editable` flags;
     - enum constants, by name and value.
   - **Self-checks:** the header `static_assert`s every id against
     `schema::typeId`/`schema::fieldId` of its names, using Core only.
   - **Never generated:** `byteOffset`, World C++ type names, or any
     layout claim about World's structs.
3. **Typed operations go through the reflective layer** (ADR-0110 D2).
   - **`read<C>`** is one get per leaf, or one `getProperties` batch when a
     `QueryBatch` is supplied.
     - It is **not a snapshot**: one-at-a-time queries over Remote are
       answered at successive frame boundaries, and the batch promises
       input order, not one instant.
     - Each leaf is as it was when answered.
     - **Pausing alone does not isolate a read.** Any client may step or
       resume the Runtime, and a step queued earlier may still be pending.
       A read is consistent only if, for its whole duration:
       - the Runtime stays paused;
       - no client calls `step` or `resume`;
       - no step is pending.

       No lock, lease or snapshot mechanism is added to enforce this.
   - **`add(entity, C)`** appends `AddComponent` then one `SetProperty` per
     leaf, in descriptor order.
     - It is declared only for a type whose every leaf is `Editable`. For
       any other type it does not compile; the client adds the bare
       component and sets the editable leaves.
     - It writes every leaf. A value-initialized `C{}` writes zeros, and
       enum value 0; if 0 is not a declared constant of that enum, the
       boundary refuses the write (`EnumValueOutOfRange`) and its
       transaction aborts. A bare add gives World's defaults.
   - **Typed event decoding** matches on the binding's ids.
4. **Compatibility is checked per type, recursively, on first use** (Spec 0057 ruling Q4).
   - Before the first typed operation that uses a type, `gameplay::World`
     compares that type's binding with the connection's `schema()`:
     - `TypeId`, kind and `schemaVersion`;
     - the exact field set (names, ids, kinds, primitive kinds, referenced
       `TypeId`s, flags);
     - enum constants by name and value.
   - **Recursively:** every struct or enum type a field references
     (`Camera` → `CameraFog`, `CameraBloom`; `Light` → `LightKind`) is
     checked the same way. A type is compatible only if its whole closure
     is.
   - **On a mismatch:**
     - every typed operation on that type returns `SchemaMismatch`,
       submitting nothing;
     - a transaction, typed or mixed, that contains any operation on an
       incompatible type is not submitted at all, takes no ticket, and
       returns `SchemaMismatch`;
     - typed decoding of that type's events returns `SchemaMismatch`.
   - The result is cached for the `World`'s lifetime. The reflective layer
     alone is never blocked.
5. **Naming:** generated C++ names are the schema's names. Types use the
   part after the module prefix (`world::Light` →
   `atlantis::gameplay::world::Light`), and fields use their schema names.
   A schema name that is not a valid C++ identifier, or is a C++ keyword,
   fails generation rather than being mangled.

## Consequences

### Positive

- Typed access with no hand-written per-component code and no reflection
  library. Wrong kinds and read-only writes are compile errors.
- The generated code is reviewable in every PR that changes the schema. Its
  freshness is enforced by a test, not by convention.
- Clients built against another Runtime fail with a named, per-type
  `SchemaMismatch`, not a stream of `KindMismatch` refusals.
- The same generator design (descriptors in, bindings out, committed and
  checked) is the template for later C# or Python bindings.
- No byte offsets cross the boundary, so World's struct layout stays
  internal (ADR-0034).

### Negative / Trade-offs

- **A schema edit becomes two steps** (edit the table, regenerate), and
  the staleness test fails until both are done. This is deliberate.
- **The generator links World**, so the tool, not the SDK, sees World's
  types. It is host-only and in Tools, like the cooker linking Asset
  System.
- **Generated value structs repeat World's field set** under SDK names.
  That duplication is the point: clients never see World's C++ types.
  The staleness test keeps it in step.
- **Per-type lazy checking** means a mismatch shows on first use, not at
  connect.
- **A component read is not a snapshot.** It is consistent only while the
  Runtime stays paused, with no `step` or `resume` from any client and no
  pending step. The contract says so rather than implying an atomicity or
  isolation the connection does not offer, and coordinating control
  between clients is left to them.
- **A value-initialized struct is not a valid default** when one of its
  enums has no 0 constant. The binding's "0 is declared" flag makes this
  visible, and a bare add remains the way to get World's defaults.

## Alternatives Considered

- **Build-time generation** (`add_custom_command`). Rejected:
  - Android cross-builds cannot run a host tool, and ADR-0080 packages
    cooked data, not code;
  - the generated API would not be reviewable in PRs.
- **Compile-time lookup with no generator** (`get<"Light.intensity">()`
  against a `constexpr` table). Rejected:
  - World's table would move into a public header;
  - there would be no named value structs, and error messages would be
    template diagnostics.
- **Hand-written typed wrappers.** Rejected: they are not generated and
  drift silently.
- **Generating descriptor tables from annotated C++** (macros or a
  parser). Rejected: it reverses ADR-0099 D4 and is the reflection system
  ADR-0099 declined.
- **Checking every type at connect and refusing the whole connection.**
  Rejected: one renamed type the client never uses would block it.
- **Checking only a type's own fields, not the types it references.**
  Rejected: a changed `CameraFog` or `LightKind` would pass the `Camera` or
  `Light` check and fail later as `KindMismatch` or wrong enum values.
- **Submitting the compatible part of a mixed transaction.** Rejected: it
  breaks the transaction's all-or-nothing meaning.
- **Generating World's member defaults into the value structs.** Rejected:
  defaults are not schema data (ADR-0099 D5), and the generator would have
  to read World's structs by layout.
- **Initializing each enum member to its first declared constant.**
  Rejected: "first" is a listing order, not a schema meaning, and it would
  silently differ from what `C{}` means for every other member.
- **A read lease or Runtime-side snapshot for consistent reads.**
  Rejected: it is a new Runtime or `RuntimeControl` capability. Pausing
  with no step or resume is enough for a client that controls the
  Runtime.
- **Emitting `byteOffset` for zero-copy reads.** Rejected: it exposes
  internal layout (ADR-0034), and the values come by copy through the
  connection anyway.
