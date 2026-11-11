using System.Collections.Generic;

namespace Atlantis.Gameplay.Assets;

public enum AssetType { Mesh, Texture, Material, Scene, Environment }
public enum AssetError { CatalogUnavailable, UnknownAsset }
/// <summary>Owned build-catalog facts; source is provenance, not a filesystem access path.</summary>
public sealed record AssetSummary(AssetGuid Guid, AssetType Type, string Source);
public sealed record AssetDetails(AssetGuid Guid, AssetType Type, string Source, uint ArtifactSchema,
                                  uint? SourceSchema, string Tool, IReadOnlyList<AssetGuid> Dependencies);

public enum PreviewError
{
    Unsupported, Unavailable, Busy, CatalogUnavailable, UnknownAsset, NotMesh, TooLarge, InvalidMesh, RenderFailed,
}

/// <summary>An owned PNG and its mesh identity. The consumer owns the byte array and decodes the image.</summary>
public sealed record MeshPreview(AssetGuid Guid, int Width, int Height, byte[] PngBytes);

public readonly record struct RebuildId(AssetGuid Value)
{
    public override string ToString() => Value.ToString();
}
public readonly record struct GenerationId(AssetGuid Value)
{
    public override string ToString() => Value.ToString();
}
public enum RebuildError { Unavailable, Busy, WorkspaceNeedsRepair, CatalogUnavailable, UnknownAsset, UnsupportedProducer, UnknownJob, Unsupported }
public enum RebuildState { Running, Succeeded, Failed }
public enum RebuildStage { Launch, Pipeline, Validation, Publication }
public enum RebuildFailureCode { LaunchFailed, BuildFailed, InvalidOutput, IdentityChanged, PublishFailed, TimedOut }
public sealed record RebuildFailure(RebuildStage Stage, RebuildFailureCode Code, string Diagnostic, uint? ExitCode);
public sealed record RebuildJob(RebuildId Id, AssetGuid Asset, RebuildState State,
                                GenerationId? Generation = null, RebuildFailure? Failure = null);
public sealed record RebuildStatus(GenerationId? ActiveGeneration, RebuildJob? Job);

public readonly record struct ImportId(AssetGuid Value)
{
    public override string ToString() => Value.ToString();
}
public enum ImportMode { Register, Resume }
public enum ImportError
{
    Unavailable, Busy, WorkspaceNeedsRepair, CatalogUnavailable, NameConflict, ActivationRequired,
    NothingToResume, UnknownJob, Unsupported,
}
public enum ImportStage { Source, Pipeline, Validation, Registration, Publication }
public enum ImportFailureCode
{
    InvalidSource, LaunchFailed, BuildFailed, InvalidOutput, IdentityConflict, RegisterFailed, PublishFailed, TimedOut,
}
public enum RegistrationState { NotCommitted, Committed, Unknown }
public sealed record ImportFailure(ImportStage Stage, ImportFailureCode Code, string Diagnostic, uint? ExitCode,
                                   RegistrationState Registration, AssetGuid? RegisteredRootGuid);
public sealed record ImportJob(ImportId Id, string Name, ImportMode Mode, RebuildState State,
                                AssetGuid? RootGuid = null, GenerationId? Generation = null, ImportFailure? Failure = null);
public sealed record ImportStatus(GenerationId? ActiveGeneration, ImportJob? Job);
