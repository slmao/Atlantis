#pragma once

namespace atlantis::asset_system {

// Plan 0012 Section D6. Every recoverable error this module returns is
// one of these enumerators, wrapped in atlantis::Result<T, E> -- never
// an exception. I/O failure, version incompatibility, and content
// failure are always distinct enumerators, never collapsed together.

enum class LogicalPathError {
  EmptyPath,
  AbsolutePathRejected,
  DriveLetterRejected,
  EscapesAssetRoot,
  DisallowedCharacter,
};

enum class AssetSetError {
  AssetIdCollision,
  CaseOnlyPathConflict,
  DuplicateLogicalPath,
  InvalidLogicalPath,
};

enum class SourceParseError {
  UnknownSourceVersion,
  MissingField,
  FieldOrderMismatch,
  MalformedNumber,
  NonFiniteFloat,
  CountMismatch,
  IndexOutOfRange,
  IndexCountNotMultipleOfThree,
  VertexCountOutOfRange,
  TrailingContent,
};

enum class MetadataParseError {
  UnknownMetadataVersion,
  WrongLineCount,
  FieldNameMismatch,
  MalformedValue,
};

enum class ArtifactDecodeError {
  TooSmallForHeader,
  BadMagic,
  UnknownSchemaVersion,
  UnsupportedVertexStride,
  InconsistentOffsets,
  SizeMismatch,
  VertexCountOutOfRange,
  IndexCountNotMultipleOfThree,
  IndexOutOfRange,
  NonFiniteFloat,
};

enum class AssetLoadError {
  ArtifactFileUnreadable,
  MetadataFileUnreadable,
  ArtifactDecodeFailed,
  MetadataParseFailed,
  MetadataArtifactMismatch,
};

enum class CookError {
  SourceFileUnreadable,
  SourceParseFailed,
  LogicalPathInvalid,
  ArtifactWriteFailed,
  MetadataWriteFailed,
};

// Plan 0015 Section D2 / ADR-0053 (including its own Human Review
// Correction, 2026-08-23): cookScene()'s own validation conditions.
// EmptyScene (node_count == 0) is checked before any of the
// per-node/reference conditions below -- ValidatedSceneData has no
// public default constructor, so this is the check that makes an
// "empty but valid" scene structurally unreachable.
enum class SceneCookError {
  SourceFileUnreadable,
  SourceParseFailed,
  EmptyScene,
  DuplicateNodeId,
  UndeclaredParentReference,
  ParentCycle,
  UndeclaredActiveCameraReference,
  ActiveCameraMissingCamera,
  NonFiniteValue,
  ArtifactWriteFailed,
  MetadataWriteFailed,
};

// decodeScene()'s own conditions -- never assumes a well-formed cooker
// output, independently re-derives every SceneCookError condition from
// the artifact's own bytes. Combines ArtifactDecodeError's own
// binary-decode granularity with AssetLoadError's own I/O/metadata-
// cross-check granularity into one enum, because decodeScene() is a
// single function playing both roles at once.
enum class SceneArtifactDecodeError {
  ArtifactUnreadable,
  MetadataUnreadable,
  TooSmallForHeader,
  BadMagic,
  UnknownSchemaVersion,
  InconsistentOffsets,
  SizeMismatch,
  EmptyScene,
  NodeCountOutOfRange,
  OutOfRangeParentIndex,
  CyclicParent,
  OutOfRangeActiveCameraIndex,
  ActiveCameraMissingCamera,
  NonFiniteValue,
  MetadataParseFailed,
  MetadataArtifactMismatch,
};

// Plan 0016 Section D8: cookTexture()'s own validation conditions --
// mirrors cookStaticMesh()'s own established set (Spec 0016 Human
// Review item 9's own explicit checked-arithmetic requirement is what
// SourceOverflow guards). A single AtomicWriteFailed, unlike CookError's
// own separate Artifact/MetadataWriteFailed pair -- cookTexture()'s own
// two atomic writes (artifact, metadata) are not independently
// distinguished by this enum. LogicalPathInvalid added by Plan 0016's
// own "Human Review Correction -- 2026-08-24": cookTexture() now
// normalizes its own logicalPathInput exactly like cookStaticMesh()
// does, so it needs the matching rejection case -- named to match
// CookError::LogicalPathInvalid exactly, not a new naming convention.
enum class TextureCookError {
  ZeroDimension,
  DimensionExceedsMaximum,
  SourceOverflow,
  LogicalPathInvalid,
  AtomicWriteFailed,
};

// decodeTextureArtifact()'s own conditions -- never assumes a
// well-formed cooker output, independently re-derives every
// TextureCookError-adjacent condition from the artifact's own bytes.
enum class TextureArtifactDecodeError {
  BadMagic,
  UnsupportedSchemaVersion,
  TruncatedHeader,
  InconsistentPixelDataSize,
  DimensionExceedsMaximum,
  UnknownFormat,
  UnsupportedMipCount,  // must equal 1
};

enum class TextureLoadError {
  ArtifactDecodeFailed,  // wraps TextureArtifactDecodeError, including a
                          // failed artifact file read
  MetadataParseFailed,
  MetadataArtifactMismatch,
  MetadataReadFailed,
};

// Plan 0018 Section P2: cookMaterial()'s own validation conditions --
// mirrors TextureCookError's own shape exactly. LogicalPathInvalid is a
// reused name, matching CookError/TextureCookError's own precedent of a
// same-named enumerator per format.
//
// Deviation from Plan 0018 Section P2 (mechanical, disclosed, no
// architectural effect): the Plan's own error-domain accounting also
// named a cook-time UnknownMaterialKind. Implementation found this
// enumerator is never actually reachable -- parseMaterialSource()
// already rejects any unrecognized "kind:" token at the grammar level
// (MaterialSourceParseError::UnknownKind) before a ParsedMaterialSource
// is ever constructed, and MaterialKind has exactly one enumerator this
// round, so cookMaterial() can never observe an "unknown" MaterialKind
// value. Kept out per this codebase's own "no speculative abstraction"
// convention (AGENTS.md) rather than shipped as untestable dead code;
// the decode-time MaterialArtifactDecodeError::UnknownMaterialKind
// below is genuinely reachable (an untrusted artifact byte buffer) and
// is unaffected.
enum class MaterialCookError {
  SourceFileUnreadable,
  SourceParseFailed,
  LogicalPathInvalid,
  AtomicWriteFailed,
};

// decodeMaterialArtifact()'s own conditions -- never assumes a
// well-formed cooker output, independently re-derives every
// MaterialCookError-adjacent condition from the artifact's own bytes.
// UnexpectedSize is new: unlike the texture artifact's own variable
// pixel payload, Material's record is fixed-size (32 bytes) by schema
// version alone, so any size other than exactly 32 is corruption, not
// merely "too small."
enum class MaterialArtifactDecodeError {
  BadMagic,
  UnsupportedSchemaVersion,
  TruncatedHeader,
  UnexpectedSize,
  UnknownMaterialKind,
  UnknownFilter,
  UnknownAddressMode,
};

enum class MaterialLoadError {
  ArtifactFileUnreadable,
  MetadataFileUnreadable,
  ArtifactDecodeFailed,
  MetadataParseFailed,
  MetadataArtifactMismatch,
};

}  // namespace atlantis::asset_system
