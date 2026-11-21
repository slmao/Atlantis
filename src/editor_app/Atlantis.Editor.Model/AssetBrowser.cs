using System;
using System.Collections.Generic;
using System.Linq;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Assets;
using Atlantis.Gameplay.Remote;

namespace Atlantis.Editor.Model;

/// <summary>A caller can retire a queued or dispatched detail intent on the UI thread.</summary>
public sealed class AssetDetailIntent
{
    private volatile bool _current = true;
    public bool Current => _current;
    internal void Cancel() => _current = false;
}

/// <summary>May be retired by the UI while a group read or guarded Add is queued.</summary>
public sealed class StaticGroupIntent(AssetGuid scene, long lifetime, bool isPrefab = false)
{
    private readonly object _gate = new();
    public AssetGuid Scene { get; } = scene;
    public long Lifetime { get; } = lifetime;
    public bool IsPrefab { get; } = isPrefab;
    private volatile bool _current = true;
    public bool Current => _current;
    public void Cancel()
    {
        lock (_gate) _current = false;
    }

    public ulong? Send(Func<ulong> send)
    {
        lock (_gate) return _current ? send() : null;
    }
}

public sealed record TextureRenameIntent(AssetSummary Asset, AssetDetailIntent Intent);
public sealed record TextureMoveIntent(AssetSummary Asset, AssetDetailIntent Intent);
public sealed record TextureRemovalIntent(AssetSummary Asset, AssetDetailIntent Intent);
public sealed record AssetDependentScan(AssetGuid Texture, IReadOnlyList<AssetSummary> Candidates,
    bool RosterComplete, GenerationId? Generation, AssetDetailIntent Intent);
public sealed record AssetReferenceRow(AssetSummary Material, TextureReferenceSlot? Slot, string? Diagnostic = null)
{
    public static string Label(TextureReferenceSlot slot) => slot switch
    {
        TextureReferenceSlot.BaseColor => "Base Color",
        TextureReferenceSlot.Normal => "Normal",
        TextureReferenceSlot.MetallicRoughness => "Metallic/Roughness",
        TextureReferenceSlot.Occlusion => "Occlusion",
        TextureReferenceSlot.Emissive => "Emissive",
        _ => throw new ArgumentOutOfRangeException(nameof(slot)),
    };
}
public sealed record AssetDependentResult(IReadOnlyList<AssetReferenceRow> Matches, bool Complete, string? Diagnostic = null);
public sealed record AssetReferenceLocate(AssetGuid Texture, AssetReferenceRow Row, GenerationId? Generation, AssetDetailIntent Intent);
public sealed record LocatedAssetReference(AssetGuid Material, TextureReferenceSlot Slot, MaterialValues Values);
public enum PrefabSourceStatus { Pending, Found, Missing, CatalogUnavailable }

public static class MaterialTextureReferences
{
    public static IEnumerable<(TextureReferenceSlot Slot, AssetGuid? Texture)> Slots(MaterialValues values)
    {
        yield return (TextureReferenceSlot.BaseColor, values.Texture);
        yield return (TextureReferenceSlot.Normal, values.NormalMap);
        yield return (TextureReferenceSlot.MetallicRoughness, values.MetallicRoughnessMap);
        yield return (TextureReferenceSlot.Occlusion, values.OcclusionMap);
        yield return (TextureReferenceSlot.Emissive, values.EmissiveTexture);
    }

    public static bool MatchesDetail(AssetSummary material, AssetDetails detail) =>
        detail.Guid == material.Guid && detail.Type == AssetType.Material && detail.Source == material.Source &&
        detail.Dependencies.All(guid => !guid.IsNil);

    public static bool ProvesSlots(AssetDetails detail, MaterialRead read, GenerationId? generation) =>
        read.Material == detail.Guid &&
        (generation is null || read.ActiveGeneration is null || generation == read.ActiveGeneration) &&
        Slots(read.Recipe.Values).Where(slot => slot.Texture is { IsNil: false })
            .Select(slot => slot.Texture!.Value).ToHashSet().SetEquals(detail.Dependencies);
}

/// <summary>UI-free local view of a fixed server snapshot. Caller-thread only;
/// request counters are local callback guards, not catalog revisions.</summary>
public sealed class AssetBrowser
{
    private IReadOnlyList<AssetSummary> _items = Array.Empty<AssetSummary>();
    private IReadOnlyList<AssetBrowseProjection> _keys = Array.Empty<AssetBrowseProjection>();
    private IReadOnlyList<AssetSummary> _visible = Array.Empty<AssetSummary>();
    private IReadOnlyList<object> _rows = Array.Empty<object>();
    private HashSet<AssetGuid> _visibleIds = new();
    private AssetType? _type;
    private string _search = "";
    private AssetBrowseSort _sort;
    private AssetBrowseGroup _group;
    private long _listRequest, _detailRequest;
    public string State { get; private set; } = "Not loaded";
    public AssetType? Type
    {
        get => _type;
        set { if (_type != value) { _type = value; Project(); } }
    }
    public string Search
    {
        get => _search;
        set
        {
            string query = value.Trim();
            if (string.Equals(_search, query, StringComparison.OrdinalIgnoreCase)) return;
            _search = query;
            Project();
        }
    }
    public AssetBrowseSort Sort
    {
        get => _sort;
        set { if (_sort != value) { _sort = value; Project(); } }
    }
    public AssetBrowseGroup Group
    {
        get => _group;
        set { if (_group != value) { _group = value; Project(); } }
    }
    public AssetGuid? Selected { get; private set; }
    public AssetDetails? Details { get; private set; }
    public string DetailState { get; private set; } = "";
    public AssetDetailIntent? DetailIntent { get; private set; }
    public IReadOnlyList<AssetSummary> Items => _items;
    public PrefabSourceStatus PrefabSource(AssetGuid guid) => State switch
    {
        "Ready" => _items.Any(asset => asset.Guid == guid && asset.IsPrefab)
            ? PrefabSourceStatus.Found : PrefabSourceStatus.Missing,
        "Not loaded" or "Disconnected" or "Loading" => PrefabSourceStatus.Pending,
        _ => PrefabSourceStatus.CatalogUnavailable,
    };
    public IReadOnlyList<AssetSummary> Visible => _visible;
    public IReadOnlyList<object> Rows => _rows;
    public bool SelectionVisible => State == "Ready" && Selected is AssetGuid guid && _visibleIds.Contains(guid);
    public bool NeedsDetail => SelectionVisible && DetailState == "";
    public string ViewState => State == "Ready"
        ? _items.Count == 0 ? "Catalog empty" : _visible.Count == 0 ? "No matching assets" : $"{_visible.Count} of {_items.Count} assets"
        : State;

    public TextureRenameIntent? RenameIntent { get; private set; }

    public TextureRenameIntent? BeginRename()
    {
        CancelRename();
        var asset = _items.FirstOrDefault(item => item.Guid == Selected);
        return !SelectionVisible || asset is null || RegisteredColorName(asset) is null ? null :
            RenameIntent = new(asset, new());
    }

    public void CancelRename()
    {
        RenameIntent?.Intent.Cancel();
        RenameIntent = null;
    }

    public TextureMoveIntent? MoveIntent { get; private set; }

    public TextureMoveIntent? BeginMove()
    {
        CancelMove();
        var asset = _items.FirstOrDefault(item => item.Guid == Selected);
        return !SelectionVisible || asset is null || RegisteredColorName(asset) is null ? null :
            MoveIntent = new(asset, new());
    }

    public void CancelMove()
    {
        MoveIntent?.Intent.Cancel();
        MoveIntent = null;
    }

    public TextureRemovalIntent? RemovalIntent { get; private set; }

    public TextureRemovalIntent? BeginRemoval()
    {
        CancelRemoval();
        var asset = _items.FirstOrDefault(item => item.Guid == Selected);
        return !SelectionVisible || RegisteredColorName(asset) is null ? null :
            RemovalIntent = new(asset!, new());
    }

    public void CancelRemoval()
    {
        RemovalIntent?.Intent.Cancel();
        RemovalIntent = null;
    }

    public AssetDependentScan? DependentScan { get; private set; }
    public AssetDependentResult? DependentResult { get; private set; }
    public AssetReferenceLocate? ReferenceLocate { get; private set; }
    public LocatedAssetReference? LocatedReference { get; private set; }
    public string? ReferenceNotice { get; private set; }
    public bool CanScanReferences => SelectionVisible && Selected is AssetGuid guid &&
        _items.Any(item => item.Guid == guid && item.Type == AssetType.Texture);

    public static string? RegisteredColorName(AssetSummary? asset)
    {
        const string prefix = "content:texture_", suffix = "/source.png";
        if (asset?.Type != AssetType.Texture || !asset.Source.StartsWith(prefix, StringComparison.Ordinal) ||
            !asset.Source.EndsWith(suffix, StringComparison.Ordinal) || asset.Source.Length <= prefix.Length + suffix.Length)
            return null;
        string name = asset.Source[prefix.Length..^suffix.Length];
        return Codec.ValidImportName(name) ? name : null;
    }

    public AssetDependentScan? BeginDependents(GenerationId? generation = null)
    {
        if (!CanScanReferences || DependentScan is not null || Selected is not AssetGuid guid) return null;
        InvalidateDependents();
        var groups = _items.ToLookup(item => item.Guid);
        var materials = groups.Where(group => !group.Key.IsNil && group.Count() == 1)
            .Select(group => group.Single()).Where(item => item.Type == AssetType.Material)
            .OrderBy(item => item.Source, StringComparer.Ordinal)
            .ThenBy(item => item.Guid.ToString(), StringComparer.Ordinal).ToArray();
        bool complete = groups.All(group => !group.Key.IsNil && group.Count() == 1);
        return DependentScan = new(guid, materials, complete, generation, new());
    }

    public bool CompleteDependents(AssetDependentScan scan, AssetDependentResult result)
    {
        if (!ReferenceEquals(scan, DependentScan) || !scan.Intent.Current || !SelectionVisible ||
            Selected != scan.Texture) return false;
        scan.Intent.Cancel();
        DependentScan = null;
        DependentResult = result;
        return true;
    }

    public void CancelPendingDependents()
    {
        if (DependentScan is null && ReferenceLocate is null) return;
        InvalidateDependents();
        ReferenceNotice = "Reference check canceled.";
    }

    public AssetReferenceLocate? BeginReferenceLocate(AssetReferenceRow row, GenerationId? generation)
    {
        if (!CanScanReferences || Selected is not AssetGuid texture || row.Slot is null || ReferenceNotice is not null ||
            DependentResult?.Matches.Any(current => ReferenceEquals(current, row)) != true) return null;
        ReferenceLocate?.Intent.Cancel();
        ReferenceNotice = null;
        return ReferenceLocate = new(texture, row, generation, new());
    }

    public bool CompleteReferenceLocate(AssetReferenceLocate request, MaterialRead? read, out LocatedAssetReference? located)
    {
        located = null;
        if (!ReferenceEquals(request, ReferenceLocate) || !request.Intent.Current || !SelectionVisible ||
            request.Texture != Selected) return false;
        request.Intent.Cancel();
        ReferenceLocate = null;
        if (read is not null && request.Row.Slot is { } slot && read.Material == request.Row.Material.Guid &&
            (request.Generation is null || read.ActiveGeneration is null || request.Generation == read.ActiveGeneration) &&
            MaterialTextureReferences.Slots(read.Recipe.Values).Any(value => value.Slot == slot && value.Texture == request.Texture))
            located = new(read.Material, slot, read.Recipe.Values);
        else
            ReferenceNotice = "Reference changed or could not be verified. Refresh references.";
        return true;
    }

    private void InvalidateDependents()
    {
        DependentScan?.Intent.Cancel();
        DependentScan = null;
        DependentResult = null;
        ReferenceLocate?.Intent.Cancel();
        ReferenceLocate = null;
        ReferenceNotice = null;
        LocatedReference = null;
    }

    private void InvalidateDetail()
    {
        ++_detailRequest;
        DetailIntent?.Cancel();
        DetailIntent = null;
        Details = null;
        DetailState = "";
    }

    private void Project()
    {
        string path = _search.Replace((char)92, '/');
        string? compact = AssetBrowseProjection.CompactQuery(_search);
        var matching = _keys.Where(key => (_type is null || key.Asset.Type == _type) &&
            key.Matches(_search, path, compact));
        var ordered = AssetBrowseProjection.Order(matching, _sort);
        _visible = ordered.Select(key => key.Asset).ToArray();
        _visibleIds = _visible.Select(asset => asset.Guid).ToHashSet();
        _rows = AssetBrowseProjection.Rows(ordered, _group);
        if (!SelectionVisible)
        {
            InvalidateDetail();
            InvalidateDependents();
            CancelRename();
            CancelMove();
            CancelRemoval();
        }
    }

    public long BeginList()
    {
        CancelRename();
        CancelMove();
        CancelRemoval();
        InvalidateDependents();
        InvalidateDetail();
        State = "Loading";
        return ++_listRequest;
    }

    public bool CompleteList(long request, Outcome<IReadOnlyList<AssetSummary>, AssetError> result)
    {
        if (request != _listRequest) return false;
        if (!result.IsOk) { State = result.Error.ToString(); return true; }
        _items = result.Value.ToArray();
        _keys = _items.Select(asset => new AssetBrowseProjection(asset)).ToArray();
        State = "Ready";
        if (Selected is AssetGuid guid && !_items.Any(asset => asset.Guid == guid)) ClearSelection();
        Project();
        return true;
    }

    public bool Reveal(AssetGuid guid)
    {
        var key = State == "Ready" ? _keys.FirstOrDefault(candidate => candidate.Asset.Guid == guid) : null;
        if (key is null) return false;
        if (_visibleIds.Contains(guid)) return true;
        if (_type != key.Asset.Type) _type = null;
        if (!key.Matches(_search, _search.Replace((char)92, '/'), AssetBrowseProjection.CompactQuery(_search))) _search = "";
        Project();
        return _visibleIds.Contains(guid);
    }

    public long Select(AssetGuid guid, LocatedAssetReference? located = null)
    {
        if (Selected != guid)
        {
            InvalidateDependents();
            CancelRename();
            CancelMove();
            CancelRemoval();
        }
        InvalidateDetail();
        Selected = guid;
        if (located?.Material == guid) LocatedReference = located;
        if (SelectionVisible)
        {
            DetailState = "Loading";
            DetailIntent = new();
        }
        return _detailRequest;
    }

    public void ClearSelection()
    {
        CancelRename();
        CancelMove();
        CancelRemoval();
        InvalidateDependents();
        InvalidateDetail();
        Selected = null;
    }

    public bool CompleteDetail(long request, AssetGuid guid, Outcome<AssetDetails, AssetError> result)
    {
        if (request != _detailRequest || Selected != guid || !SelectionVisible || DetailIntent?.Current != true) return false;
        Details = result.IsOk ? result.Value : null;
        DetailState = result.IsOk ? "Ready" : result.Error.ToString();
        return true;
    }

    public bool InterruptReferences()
    {
        if (DependentScan is null && ReferenceLocate is null && DependentResult is null) return false;
        var rows = DependentResult?.Matches ?? Array.Empty<AssetReferenceRow>();
        bool checkingSlot = ReferenceLocate is not null;
        InvalidateDependents();
        DependentResult = new(rows, false, "Runtime disconnected. Reconnect and refresh references.");
        if (checkingSlot) ReferenceNotice = "Reference could not be verified. Reconnect and refresh references.";
        CancelRename();
        CancelMove();
        CancelRemoval();
        DetailIntent?.Cancel();
        return true;
    }

    public void Disconnect()
    {
        ++_listRequest;
        ClearSelection();
        _items = Array.Empty<AssetSummary>();
        _keys = Array.Empty<AssetBrowseProjection>();
        _visible = Array.Empty<AssetSummary>();
        _rows = Array.Empty<object>();
        _visibleIds.Clear();
        State = "Disconnected";
        DetailState = "Disconnected";
    }
}

// Surface 0 is Assets. A chooser uses its unique request ID.
public readonly record struct AssetPreviewInputs(AssetGuid Mesh, AssetGuid? Material, long Surface = 0, MaterialRecipe? Draft = null, bool WithMaps = false, bool WithMetallicRoughness = false, bool WithOcclusion = false, bool WithCombinedMaps = false);
public sealed record AssetPreviewIntent(long Epoch, AssetGuid Guid, AssetGuid? Material, long Surface, MaterialRecipe? Draft = null, bool WithMaps = false, bool WithMetallicRoughness = false, bool WithOcclusion = false, bool WithCombinedMaps = false);

/// <summary>One active operation and one replaceable intent across Editor
/// sessions. UI transitions and the worker's admission check share a lock;
/// connection setup, RPC and image decoding run outside it.</summary>
public sealed class AssetPreviewRequests
{
    private readonly object _gate = new();
    private long _epoch;
    private AssetPreviewInputs? _inputs;
    private AssetPreviewIntent? _current, _next, _active;

    public bool SetInputs(AssetPreviewInputs? inputs, bool invalidate = false)
    {
        lock (_gate)
        {
            if (!invalidate && _inputs == inputs) return false;
            _inputs = inputs;
            ++_epoch;
            _current = _next = null;
            return true;
        }
    }

    public bool CanRequest(bool withMaterial = false)
    {
        lock (_gate) return CanRequestLocked(withMaterial);
    }

    private bool CanRequestLocked(bool withMaterial)
    {
        if (_inputs is not AssetPreviewInputs inputs ||
            (withMaterial ? inputs.Material is null : inputs.Surface != 0)) return false;
        return _current is null || _current.Material != (withMaterial ? inputs.Material : null) ||
            (_current != _active && _current != _next);
    }

    public bool Request(bool withMaterial = false)
    {
        lock (_gate)
        {
            if (!CanRequestLocked(withMaterial) || _inputs is not AssetPreviewInputs inputs) return false;
            _current = _next = new(++_epoch, inputs.Mesh, withMaterial ? inputs.Material : null, inputs.Surface);
            return true;
        }
    }

    public bool CanRequestDraft()
    {
        lock (_gate) return _inputs?.Draft is not null && (_current is null || (_current != _active && _current != _next));
    }

    public bool RequestDraft()
    {
        lock (_gate)
        {
            if (_inputs is not { Draft: { } draft } inputs || !CanRequestDraft()) return false;
            _current = _next = new(++_epoch, inputs.Mesh, null, inputs.Surface, draft, inputs.WithMaps, inputs.WithMetallicRoughness, inputs.WithOcclusion, inputs.WithCombinedMaps);
            return true;
        }
    }

    public AssetPreviewIntent? TakeNext()
    {
        lock (_gate)
        {
            if (_active is not null) return null;
            _active = _next;
            _next = null;
            return _active;
        }
    }

    // This is the send admission point after setup. An invalidation that
    // wins the lock prevents the call; a call admitted first may finish.
    public bool CanSend(AssetPreviewIntent intent)
    {
        lock (_gate) return _active == intent && _current == intent;
    }

    public bool Settle(AssetPreviewIntent intent)
    {
        lock (_gate)
        {
            if (_active != intent) return false;
            _active = null;
            return _current == intent;
        }
    }
}

// The Assets session owns the receipt even after the material window closes.
public sealed class MaterialSettlement
{
    private MaterialJob? _previousCommittedJob;
    public MaterialTarget? Target { get; private set; }
    public MaterialJobId? Id { get; private set; }
    public MaterialJob? Job { get; private set; }
    public bool AwaitingAdmission { get; private set; }
    public bool Unknown { get; private set; }
    public bool NeedsRepair { get; private set; }
    public bool InFlight => AwaitingAdmission || !Unknown && Job?.State == RebuildState.Running;
    public bool BlocksSave => InFlight || Unknown || NeedsRepair || Job?.State == RebuildState.Succeeded ||
        Job?.Failure?.SourceState is RegistrationState.Committed or RegistrationState.Unknown ||
        _previousCommittedJob is not null;
    public AssetGuid? ResumableMaterial => InFlight || Unknown || NeedsRepair ||
        Job?.Failure?.SourceState == RegistrationState.Unknown ? null :
        CommittedJob?.Failure?.SavedMaterial;
    private MaterialJob? CommittedJob => Job?.Failure?.SourceState == RegistrationState.Committed
        ? Job : _previousCommittedJob;
    public void Begin(MaterialTarget target)
    {
        var committed = CommittedJob;
        _previousCommittedJob = target.Mode == MaterialMode.Resume &&
            committed?.Failure?.SavedMaterial == target.Material ? committed : null;
        Target = target; Id = null; Job = null; AwaitingAdmission = true; Unknown = false;
    }
    public void Refused()
    {
        if (!AwaitingAdmission) return;
        AwaitingAdmission = false;
        if (_previousCommittedJob is { } previous)
        {
            Target = previous.Target; Id = previous.Id; Job = previous;
        }
        _previousCommittedJob = null;
    }
    public bool Admit(MaterialJob job)
    {
        if (!AwaitingAdmission || job.Target != Target || job.State != RebuildState.Running) return false;
        AwaitingAdmission = false; Job = job; Id = job.Id;
        return true;
    }
    public bool Observe(MaterialStatus status)
    {
        if (Id is not MaterialJobId id || status.Job is not { } job || job.Id != id || job.Target != Target) return false;
        Job = job; Unknown = false; NeedsRepair = status.WorkspaceNeedsRepair;
        if (job.State == RebuildState.Succeeded || job.Failure?.SourceState == RegistrationState.Committed)
            _previousCommittedJob = null;
        return true;
    }
    public void LoseReceipt()
    {
        if (!InFlight) return;
        AwaitingAdmission = false; Unknown = true;
    }
}

public sealed record MaterialDraft(MaterialMode Mode, MaterialRead? Original)
{
    public MaterialValues Initial => Original?.Recipe.Values ?? new(default, 1f, 1f, 1f, 0f, 0.5f);
    public MaterialRecipe Recipe(MaterialValues values) =>
        new(values, Original?.Recipe.Filter ?? MaterialFilter.Linear, Original?.Recipe.Address ?? MaterialAddress.Repeat);
    public MaterialSave Save(string name, MaterialValues values) => Mode switch
    {
        MaterialMode.Create => new MaterialSave.Create(name, values),
        MaterialMode.Copy when Original is { } source => new MaterialSave.Copy(name, source.Material, values),
        MaterialMode.Edit when Original is { Editable: true } source =>
            new MaterialSave.Edit(source.Material, source.ActiveGeneration, source.Recipe.Values, values),
        _ => throw new InvalidOperationException("The draft has no eligible source."),
    };
}
