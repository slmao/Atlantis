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
