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

}  // namespace atlantis::asset_system
