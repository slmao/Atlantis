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

}  // namespace atlantis::asset_system
