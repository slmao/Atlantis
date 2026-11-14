using System;
using System.Collections.Generic;
using System.Linq;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Assets;

namespace Atlantis.Editor.Model;

/// <summary>UI-free local view of a fixed server snapshot. Caller-thread only;
/// request counters are local callback guards, not catalog revisions.</summary>
public sealed class AssetBrowser
{
    private IReadOnlyList<AssetSummary> _items = Array.Empty<AssetSummary>();
    private long _listRequest, _detailRequest;
    public string State { get; private set; } = "Not loaded";
    public AssetType? Type { get; set; }
    public string Search { get; set; } = "";
    public AssetGuid? Selected { get; private set; }
    public AssetDetails? Details { get; private set; }
    public string DetailState { get; private set; } = "";
    public IReadOnlyList<AssetSummary> Items => _items;
    public IReadOnlyList<AssetSummary> Visible => _items.Where(v => (Type is null || v.Type == Type) &&
        (v.Source.Contains(Search, StringComparison.OrdinalIgnoreCase) || v.Guid.ToString().Contains(Search, StringComparison.OrdinalIgnoreCase))).ToList();
    public string ViewState => State == "Ready" ? (_items.Count == 0 ? "Catalog empty" : Visible.Count == 0 ? "No matching assets" : _items.Count + " assets") : State;
    public long BeginList() { ++_detailRequest; State = "Loading"; return ++_listRequest; }
    public bool CompleteList(long request, Outcome<IReadOnlyList<AssetSummary>, AssetError> result)
    {
        if (request != _listRequest) return false;
        if (!result.IsOk) { State = result.Error.ToString(); return true; }
        _items = result.Value.ToList(); State = "Ready";
        if (Selected is AssetGuid selected && !_items.Any(v => v.Guid == selected))
        { Selected = null; Details = null; DetailState = ""; ++_detailRequest; }
        return true;
    }
    public long Select(AssetGuid guid)
    { Selected = guid; Details = null; DetailState = "Loading"; return ++_detailRequest; }
    public void ClearSelection()
    { ++_detailRequest; Selected = null; Details = null; DetailState = ""; }
    public bool CompleteDetail(long request, AssetGuid guid, Outcome<AssetDetails, AssetError> result)
    {
        if (request != _detailRequest || Selected != guid) return false;
        Details = result.IsOk ? result.Value : null;
        DetailState = result.IsOk ? "Ready" : result.Error.ToString();
        return true;
    }
    public void Disconnect()
    { ++_listRequest; ++_detailRequest; State = "Disconnected"; Details = null; DetailState = "Disconnected"; }
}

// Surface 0 is Assets. A chooser uses its unique request ID.
public readonly record struct AssetPreviewInputs(AssetGuid Mesh, AssetGuid? Material, long Surface = 0, MaterialRecipe? Draft = null, bool WithMaps = false);
public sealed record AssetPreviewIntent(long Epoch, AssetGuid Guid, AssetGuid? Material, long Surface, MaterialRecipe? Draft = null, bool WithMaps = false);

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
            _current = _next = new(++_epoch, inputs.Mesh, null, inputs.Surface, draft, inputs.WithMaps);
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
