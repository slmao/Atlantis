#pragma once

#include <atlantis/schema.h>

#include <cstdint>
#include <span>
#include <string_view>

namespace atlantis::asset_system::scene {

// Spec 0049 R3/R4/R6 / ADR-0100 D4/D5/D8: the scene semantic schema -- the
// one normative statement of what an authored scene means. v1 states exactly
// what scene source v7 and artifact 7 accept today (Plan 0049's R4 trace).
// The serializers (scene_source, scene_artifact) declare the semantic version
// they encode and are held to this schema by conformance tests (ruling Q3).
// Domains and constraints are scene-local data; Core's vocabulary is
// unchanged (ruling Q7). Immutable static data, safe for concurrent reads.

// Bumped, with the fingerprint pin, on any semantic change (ruling Q2).
inline constexpr std::uint32_t kSemanticVersion = 1;

enum class Cardinality : std::uint8_t { Required, Optional };

// Transform is Required; Camera, Renderable and Light are Optional and share
// exclusivity group 1 -- at most one of them per node (Plan 0049 ruling J5).
struct ComponentRule {
  schema::TypeId component;
  Cardinality cardinality = Cardinality::Optional;
  std::uint8_t exclusiveGroup = 0;  // 0 = in no group
};

// Every kind implies finite. Closed is [min, max]; AtLeast is [min, +inf).
// NonNilGuid applies to a GUID field (when present, for an Optional one).
// Enumerated means one of the field's enum type's declared constants.
enum class DomainKind : std::uint8_t { Finite, Closed, AtLeast, NonNilGuid, Enumerated };

// One per non-struct field of the semantic component types. For a
// Vec3Float32 field the domain applies to each component.
struct FieldDomain {
  schema::FieldId field;
  DomainKind kind = DomainKind::Finite;
  float min = 0.0f;  // Closed, AtLeast
  float max = 0.0f;  // Closed
};

enum class Constraint : std::uint8_t {
  NonEmptyDocument,
  NodeGuidNonNil,
  NodeGuidUnique,
  ParentExists,
  ParentAcyclic,
  ActiveCameraExists,
  ActiveCameraHasCamera,
  ComponentExclusivity,
  LightRangeMatchesKind,  // Point: range > 0; Directional: range == 0
  MaxDirectionalLights,   // limit
  MaxPointLights,         // limit
};

struct ConstraintRule {
  Constraint id = Constraint::NonEmptyDocument;
  std::uint32_t limit = 0;  // MaxDirectionalLights, MaxPointLights; else 0
};

struct SceneSchema {
  std::uint32_t semanticVersion = kSemanticVersion;
  std::span<const ComponentRule> components;
  std::span<const FieldDomain> domains;
  std::span<const ConstraintRule> constraints;
};

[[nodiscard]] const SceneSchema& sceneSchema() noexcept;

// Plan 0049 P7: FNV-1a-64 over a canonical serialization of sceneSchema()
// and every descriptor its components reach (ids, kinds, per-type
// SchemaVersions, flags, enum constants, domains, constraints) -- excluding
// byte offsets, member order, descriptor names and the version itself. Any
// semantic change alters it; a test pins it with kSemanticVersion.
[[nodiscard]] std::uint64_t sceneSemanticFingerprint();

[[nodiscard]] std::string_view toString(Constraint constraint) noexcept;
[[nodiscard]] std::string_view toString(DomainKind kind) noexcept;

}  // namespace atlantis::asset_system::scene
