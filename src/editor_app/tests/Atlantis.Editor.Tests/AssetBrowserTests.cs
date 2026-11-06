using System;
using System.Collections.Generic;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Assets;

namespace Atlantis.Editor.Tests;
public static partial class Program
{
    private static void AssetBrowserChecks()
    {
        AssetGuid a = AssetGuid.Parse("00810081-0081-4081-8081-008100810081"), b = AssetGuid.Parse("00810081-0081-4081-8081-008100810082");
        var model = new AssetBrowser();
        var data = new[] { new AssetSummary(a, AssetType.Mesh, "content:Scene.gltf#mesh/0"), new AssetSummary(b, AssetType.Material, "assets:material.txt") };
        var ok = Outcome<IReadOnlyList<AssetSummary>, AssetError>.Ok(data);
        long old = model.BeginList(), current = model.BeginList();
        Check(!model.CompleteList(old, ok), "assets: superseded view ignored");
        Check(model.CompleteList(current, ok) && model.Visible.Count == 2, "assets: complete list published");
        model.Search = "SCENE"; Check(model.Visible.Count == 1, "assets: ordinal case-insensitive source search");
        model.Type = AssetType.Material; Check(model.ViewState == "No matching assets", "assets: no match differs from empty catalog");
        model.Search = ""; model.Type = null;
        long before = model.Select(a), after = model.Select(b);
        var detail = new AssetDetails(a, AssetType.Mesh, data[0].Source, 7, null, "test", Array.Empty<AssetGuid>());
        Check(!model.CompleteDetail(before, a, Outcome<AssetDetails, AssetError>.Ok(detail)), "assets: stale detail ignored");
        Check(model.CompleteDetail(after, b, Outcome<AssetDetails, AssetError>.Err(AssetError.UnknownAsset)) && model.DetailState == "UnknownAsset", "assets: lookup failure visible");
        current = model.BeginList();
        Check(!model.CompleteDetail(after, b, Outcome<AssetDetails, AssetError>.Err(AssetError.UnknownAsset)), "assets: obsolete view detail ignored");
        model.CompleteList(current, ok);
        Check(model.Selected == b, "assets: reload preserves GUID selection");
        old = model.BeginList(); model.Disconnect();
        Check(!model.CompleteList(old, ok) && model.ViewState == "Disconnected", "assets: disconnected callbacks ignored");
        current = model.BeginList(); model.CompleteList(current, Outcome<IReadOnlyList<AssetSummary>, AssetError>.Err(AssetError.CatalogUnavailable));
        Check(model.ViewState == "CatalogUnavailable", "assets: unavailable differs from empty");
        current = model.BeginList(); model.CompleteList(current, Outcome<IReadOnlyList<AssetSummary>, AssetError>.Ok(Array.Empty<AssetSummary>()));
        Check(model.ViewState == "Catalog empty" && model.Selected is null, "assets: successful empty clears missing selection");

        var reloadModel = new AssetBrowser();
        reloadModel.CompleteList(reloadModel.BeginList(), ok);
        long pendingDetail = reloadModel.Select(a);
        long reload = reloadModel.BeginList();
        Check(reloadModel.CompleteList(reload, ok) && reloadModel.Selected == a,
              "assets: pending-detail reload preserves the selected GUID");
        long refreshedDetail = reloadModel.Select(reloadModel.Selected!.Value);
        var refreshed = detail with { Tool = "reload result" };
        Check(reloadModel.CompleteDetail(refreshedDetail, a, Outcome<AssetDetails, AssetError>.Ok(refreshed)) &&
              reloadModel.DetailState == "Ready", "assets: replacement detail settles after reload");
        Check(!reloadModel.CompleteDetail(pendingDetail, a, Outcome<AssetDetails, AssetError>.Ok(detail)) &&
              ReferenceEquals(reloadModel.Details, refreshed), "assets: late old reply for the same GUID cannot overwrite reload result");
    }
}
