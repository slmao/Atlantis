# ADR 0111: Schema-Generated Typed Bindings

- **Status:** Proposed
- **Date:** 2026-10-08
- **Deciders:** slmao
- **Acceptance:** pending (review of Spec 0057's own branch PR)
- **Related Spec:** [Spec 0057: Gameplay SDK](../specs/0057-gameplay-sdk.md) (`In Review`)
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

1. **A host generator writes the bindings; the output is committed.**
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
2. **The generated shape.**
   - **Values:**
     - one plain value struct per schema struct, nested structs as
       members of their generated type;
     - one `enum class` per schema enum, its enumerators carrying the
       constants' values;
     - field types exactly the `PropertyValue` alternatives
       (`std::uint64_t`, `float`, `std::array<float, 3>`,
       `std::array<float, 4>`, `AssetGuid`, `EntityGuid`);
     - `Optional` → `std::optional<T>`;
     - member defaults are value-initialized, not World's defaults (the
       schema carries no defaults, ADR-0099 D5).
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
     - each field's `FieldId`, kind, primitive kind, `Optional` and
       `Editable` flags;
     - enum constants.
   - **Self-checks:** the header `static_assert`s every id against
     `schema::typeId`/`schema::fieldId` of its names, using Core only.
   - **Never generated:** `byteOffset`, World C++ type names, or any
     layout claim about World's structs.
3. **Typed operations go through the reflective layer** (ADR-0110 D2).
   - `read<C>` is one get per leaf.
   - `add(entity, C)` appends `AddComponent` then one `SetProperty` per
     leaf, in descriptor order.
   - A typed event decoding matches on the binding's ids.
4. **Compatibility is checked per type, on first use.**
   - Before its first typed operation on a type, `gameplay::World` compares
     that type's binding with the connection's `schema()`: ids, kinds,
     flags, constants and version.
   - A mismatch makes every typed operation on that type return
     `SchemaMismatch`, submitting nothing.
   - The result is cached for the `World`'s lifetime. The reflective layer
     is never blocked.
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
- **Emitting `byteOffset` for zero-copy reads.** Rejected: it exposes
  internal layout (ADR-0034), and the values come by copy through the
  connection anyway.
