using System;
using System.Collections.Generic;
using System.Linq;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Assets;
using Atlantis.Gameplay.Authoring;

namespace Atlantis.Editor.Model;

public sealed class SceneStatisticsIntent(EntityGuid? root, object? rootIdentity, long lifetime)
{
    private volatile bool _current = true;
    public EntityGuid? Root { get; } = root;
    public object? RootIdentity { get; } = rootIdentity;
    public long Lifetime { get; } = lifetime;
    public bool Current => _current;
    public void Cancel() => _current = false;
}

public sealed record MissingSceneBinding(SceneAddress Address, string NodeName, string FieldName, AssetGuid Asset,
    object? NodeIdentity, object? ComponentIdentity)
{
    public bool CanLocate => NodeIdentity is not null && ComponentIdentity is not null;
    public override string ToString() => $"{NodeName} ({Address.Node}) · Renderable.{FieldName} · {Asset}";
}

public sealed record SceneStatisticsReport(string Scope, AssetGuid? Scene, DocumentToken? Document,
    ulong? Nodes, ulong DrawItems, ulong Triangles, ulong TextureBytes, int Textures,
    bool DrawComplete, bool TrianglesComplete, bool TexturesComplete, bool ReferencesComplete,
    IReadOnlyList<MissingSceneBinding> Missing, IReadOnlyList<string> Reasons, ulong? Projection = null)
{
    public static SceneStatisticsReport Unavailable(string scope, string reason, ulong? projection = null) =>
        new(scope, null, null, null, 0, 0, 0, 0, false, false, false, false, [], [reason], projection);
}

public sealed class SceneStatistics
{
    public const int NodeLimit = 8192;
    public const int AssetLimit = 4096;
    public const int NodeSliceSize = 256;
    public const int MaterialSliceSize = 32;

    private readonly HashSet<AssetGuid> _observed = new();
    private readonly Dictionary<AssetGuid, uint> _meshInstances = new();
    private readonly HashSet<AssetGuid> _materials = new();
    private readonly HashSet<AssetGuid> _textures = new();
    private readonly List<MissingSceneBinding> _missing = new();
    private readonly HashSet<string> _reasons = new(StringComparer.Ordinal);
    private bool _drawComplete = true, _trianglesComplete = true, _texturesComplete = true, _referencesComplete = true;
    private ulong _draw, _triangles, _bytes;
    private int _texturesMeasured;

    public IReadOnlyDictionary<AssetGuid, AssetType>? Catalog { get; set; }
    public IEnumerable<AssetGuid> Materials => _materials;
    public IEnumerable<AssetGuid> Meshes => _meshInstances.Keys;
    public IEnumerable<AssetGuid> Textures => _textures;

    public static NodeSummary[]? SelectScope(IReadOnlyList<NodeSummary> nodes, EntityGuid? root)
    {
        if (nodes.Select(n => n.Node).Distinct().Count() != nodes.Count) return null;
        if (root is null) return nodes.ToArray();
        var first = nodes.FirstOrDefault(n => n.Node == root);
        if (first.Node != root) return null;
        var children = nodes.Where(n => n.Parent.HasValue).ToLookup(n => n.Parent!.Value);
        var all = nodes.Select(n => n.Node).ToHashSet();
        if (nodes.Any(n => n.Parent is { } parent && !all.Contains(parent))) return null;
        var selected = new List<NodeSummary> { first };
        var visited = new HashSet<EntityGuid> { first.Node };
        for (int index = 0; index < selected.Count; ++index)
            foreach (var child in children[selected[index].Node])
            {
                if (!visited.Add(child.Node)) return null;
                selected.Add(child);
            }
        return selected.ToArray();
    }

    public void IncompleteBindings(string reason)
    {
        _drawComplete = _trianglesComplete = _texturesComplete = _referencesComplete = false;
        _reasons.Add(reason);
    }

    public void Renderable(NodeSummary node, SceneAddress mesh, PropertyValue? meshValue,
        SceneAddress material, PropertyValue? materialValue, object? nodeIdentity,
        object? componentIdentity)
    {
        var meshGuid = Resolve(mesh, meshValue, AssetType.Mesh);
        bool fallback = materialValue is { IsAbsent: true };
        var materialGuid = fallback ? null : Resolve(material, materialValue, AssetType.Material);
        if (meshGuid is AssetGuid verifiedMesh)
            _meshInstances[verifiedMesh] = _meshInstances.GetValueOrDefault(verifiedMesh) + 1;
        else _trianglesComplete = false;
        if (materialGuid is AssetGuid verifiedMaterial) _materials.Add(verifiedMaterial);
        else if (!fallback) _texturesComplete = false;
        if (meshGuid.HasValue && (fallback || materialGuid.HasValue)) ++_draw;
        else _drawComplete = false;

        AssetGuid? Resolve(SceneAddress address, PropertyValue? value, AssetType expected)
        {
            if (value is not { } property || !property.TryGetAsset(out var guid) || guid.IsNil)
            {
                _referencesComplete = false;
                _reasons.Add("One or more binding values could not be read.");
                return null;
            }
            if (!Track(guid)) { _referencesComplete = false; return null; }
            if (Catalog is null)
            {
                _referencesComplete = false;
                _reasons.Add("Running Runtime catalog unavailable.");
                return null;
            }
            if (!Catalog.TryGetValue(guid, out var type))
            {
                _missing.Add(new(address, string.IsNullOrEmpty(node.Name) ? "Unnamed node" : node.Name,
                    expected == AssetType.Mesh ? "meshAsset" : "materialAsset", guid, nodeIdentity, componentIdentity));
                _reasons.Add("Known missing bindings reduce the content totals.");
                return null;
            }
            if (type == expected) return guid;
            _referencesComplete = false;
            _reasons.Add("A binding has an unexpected catalog type.");
            return null;
        }
    }

    public void Material(AssetGuid guid, AssetDetails? details)
    {
        if (details is not { Type: AssetType.Material } || details.Guid != guid)
        {
            _texturesComplete = false;
            _reasons.Add("One or more material dependency lists could not be read.");
            return;
        }
        foreach (var texture in details.Dependencies)
        {
            if (!texture.IsNil && Track(texture) && Catalog is not null &&
                Catalog.TryGetValue(texture, out var type) && type == AssetType.Texture)
                _textures.Add(texture);
            else
            {
                _texturesComplete = false;
                _reasons.Add("A material has a missing or unsupported texture dependency.");
            }
        }
    }

    public void UnavailableMetrics(IEnumerable<AssetGuid> guids, string reason)
    {
        foreach (var guid in guids) Metric(guid, null);
        _reasons.Add(reason);
    }

    public void Metric(AssetGuid guid, ContentMetric? value)
    {
        if (_meshInstances.TryGetValue(guid, out var count))
        {
            if (value is MeshContentMetrics mesh) _triangles += (ulong)count * mesh.IndexedTriangles;
            else
            {
                _trianglesComplete = false;
                _reasons.Add("One or more cooked mesh headers are unavailable.");
            }
        }
        if (_textures.Contains(guid))
        {
            if (value is TextureContentMetrics texture)
            {
                _bytes += texture.PixelPayloadBytes;
                ++_texturesMeasured;
            }
            else
            {
                _texturesComplete = false;
                _reasons.Add("One or more cooked texture headers are unavailable.");
            }
        }
    }

    private bool Track(AssetGuid guid)
    {
        if (_observed.Contains(guid)) return true;
        if (_observed.Count < AssetLimit) return _observed.Add(guid);
        _reasons.Add("The distinct asset limit was reached.");
        return false;
    }

    public SceneStatisticsReport Report(string scope, AssetGuid? scene, DocumentToken? document, ulong? nodes,
        ulong? projection = null) =>
        new(scope, scene, document, nodes, _draw, _triangles, _bytes, _texturesMeasured,
            _drawComplete, _trianglesComplete, _texturesComplete, _referencesComplete,
            _missing.ToArray(), _reasons.Order(StringComparer.Ordinal).ToArray(), projection);
}
