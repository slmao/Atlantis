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

public enum MeshMaterialPreviewError
{
    Unsupported, Unavailable, Busy, CatalogUnavailable, UnknownMesh, UnknownMaterial, NotMesh, NotMaterial,
    InvalidMesh, InvalidMaterial, UnsupportedMaterial, DependencyUnavailable, IncompatiblePair, TooLarge, RenderFailed,
}

/// <summary>An owned PNG for an ordered mesh and material pair. The consumer owns the byte array and decodes the image.</summary>
public sealed record MeshMaterialPreview(AssetGuid MeshGuid, AssetGuid MaterialGuid, int Width, int Height, byte[] PngBytes);

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

public readonly record struct TextureImportId(AssetGuid Value)
{
    public override string ToString() => Value.ToString();
}
public sealed record TextureImportJob(TextureImportId Id, string Name, ImportMode Mode, RebuildState State,
    AssetGuid? RootGuid, GenerationId? Generation, ImportFailure? Failure);
public sealed record TextureImportStatus(GenerationId? ActiveGeneration, TextureImportJob? Job, bool WorkspaceNeedsRepair);


// Spec 0094: the editable subset of one opaque direct-lit PBR material.
public readonly record struct MaterialValues(AssetGuid Texture, float Red, float Green, float Blue,
                                             float Metallic, float Roughness);
public enum MaterialFilter { Nearest, Linear }
public enum MaterialAddress { Repeat, ClampToEdge }
public sealed record MaterialRecipe(MaterialValues Values, MaterialFilter Filter, MaterialAddress Address);
public sealed record MaterialRead(AssetGuid Material, MaterialRecipe Recipe, bool Editable, GenerationId? ActiveGeneration);

public enum MaterialMode { Create, Copy, Edit, Resume }
public sealed record MaterialTarget(MaterialMode Mode, string? Name = null, AssetGuid? Material = null);
public abstract record MaterialSave
{
    private MaterialSave() { }
    public sealed record Create(string Name, MaterialValues Values) : MaterialSave;
    public sealed record Copy(string Name, AssetGuid Material, MaterialValues Values) : MaterialSave;
    public sealed record Edit(AssetGuid Material, GenerationId? ActiveGeneration, MaterialValues Expected,
                              MaterialValues Values) : MaterialSave;
    public sealed record Resume(AssetGuid Material) : MaterialSave;
    public MaterialTarget Target => this switch
    {
        Create x => new(MaterialMode.Create, x.Name),
        Copy x => new(MaterialMode.Copy, x.Name, x.Material),
        Edit x => new(MaterialMode.Edit, Material: x.Material),
        Resume x => new(MaterialMode.Resume, Material: x.Material),
        _ => throw new System.InvalidOperationException(),
    };
}
public readonly record struct MaterialJobId(AssetGuid Value)
{
    public override string ToString() => Value.ToString();
}
public enum MaterialError
{
    Unsupported, Unavailable, Busy, WorkspaceNeedsRepair, CatalogUnavailable, UnknownMaterial,
    NotMaterial, InvalidMaterial, UnsupportedMaterial, DependencyUnavailable, InvalidRecipe,
    UnknownMesh, NotMesh, InvalidMesh, IncompatiblePair, TooLarge, RenderFailed,
    NameConflict, StaleEdit, ActivationRequired, NothingToResume, Unchanged, UnknownJob, NotEditable,
}
public enum MaterialStage { Source, Pipeline, Validation, SourceCommit, Publication }
public enum MaterialFailureCode
{
    InvalidSource, InvalidRecipe, DependencyUnavailable, NotEditable, NameConflict, StaleEdit, ActivationRequired,
    NothingToResume, Unchanged, LaunchFailed, BuildFailed, InvalidOutput, IdentityConflict, CommitFailed,
    PublishFailed, TimedOut,
}
public sealed record MaterialFailure(MaterialStage Stage, MaterialFailureCode Code, string Diagnostic,
    uint? ExitCode, RegistrationState SourceState, AssetGuid? SavedMaterial);
public sealed record MaterialJob(MaterialJobId Id, MaterialTarget Target, RebuildState State,
    AssetGuid? SavedMaterial = null, GenerationId? Generation = null, MaterialFailure? Failure = null);
public sealed record MaterialStatus(GenerationId? ActiveGeneration, MaterialJob? Job, bool WorkspaceNeedsRepair);
public sealed record MaterialDraftPreview(AssetGuid Mesh, MaterialRecipe Recipe, int Width, int Height, byte[] PngBytes);
