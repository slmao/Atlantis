using System.Collections.Generic;

namespace Atlantis.Gameplay.Assets;

public enum AssetType { Mesh, Texture, Material, Scene, Environment }
public enum AssetError { CatalogUnavailable, UnknownAsset }
/// <summary>Owned build-catalog facts; source is provenance, not a filesystem access path.</summary>
public sealed record AssetSummary(AssetGuid Guid, AssetType Type, string Source);
public sealed record AssetDetails(AssetGuid Guid, AssetType Type, string Source, uint ArtifactSchema,
                                  uint? SourceSchema, string Tool, IReadOnlyList<AssetGuid> Dependencies);
