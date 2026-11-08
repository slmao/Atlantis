using System.Collections.Generic;

namespace Atlantis.Gameplay.Assets;

public enum AssetType { Mesh, Texture, Material, Scene, Environment }
public enum AssetError { CatalogUnavailable, UnknownAsset }
/// <summary>Owned build-catalog facts; source is provenance, not a filesystem access path.</summary>
public sealed record AssetSummary(AssetGuid Guid, AssetType Type, string Source);
public sealed record AssetDetails(AssetGuid Guid, AssetType Type, string Source, uint ArtifactSchema,
                                  uint? SourceSchema, string Tool, IReadOnlyList<AssetGuid> Dependencies);

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
