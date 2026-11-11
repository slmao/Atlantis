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

public sealed record AssetPreviewIntent(long Epoch, AssetGuid Guid);

/// <summary>One active operation and one replaceable intent across Editor
/// sessions. UI transitions and the worker's admission check share a lock;
/// connection setup, RPC and image decoding run outside it.</summary>
public sealed class AssetPreviewRequests
{
    private readonly object _gate = new();
    private long _epoch;
    private AssetGuid? _selection;
    private AssetPreviewIntent? _current, _next, _active;

    public bool SetSelection(AssetGuid? selection, bool invalidate = false)
    {
        lock (_gate)
        {
            if (!invalidate && _selection == selection) return false;
            _selection = selection;
            ++_epoch;
            _current = _next = null;
            return true;
        }
    }

    public bool CanRequest
    {
        get { lock (_gate) return CanRequestLocked(); }
    }

    private bool CanRequestLocked() => _selection.HasValue &&
        (_current is null || (_current != _active && _current != _next));

    public bool Request()
    {
        lock (_gate)
        {
            if (!CanRequestLocked() || _selection is not AssetGuid guid) return false;
            _current = _next = new(++_epoch, guid);
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
