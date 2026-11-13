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

        var settlement = new MaterialSettlement();
        var target = new MaterialTarget(MaterialMode.Edit, Material: b);
        settlement.Begin(target);
        var receipt = new MaterialJob(new(a), target, RebuildState.Running);
        Check(settlement.Admit(receipt) && settlement.BlocksSave, "material: matching admitted job blocks duplicate submission");
        settlement.LoseReceipt();
        Check(settlement.Unknown && settlement.Id == receipt.Id &&
            !settlement.Observe(new(null, receipt with { Id = new(b) }, false)),
            "material: dropped status retains the job and an unrelated latest cannot settle it");
        var failed = receipt with { State = RebuildState.Failed, Failure = new(MaterialStage.Publication,
            MaterialFailureCode.PublishFailed, "failed", null, RegistrationState.Committed, b) };
        Check(settlement.Observe(new(null, failed, false)) && settlement.BlocksSave && !settlement.Unknown,
            "material: committed failure requires resume, not another save");

        var draftPreviews = new AssetPreviewRequests();
        var recipe = new MaterialRecipe(new(b, 1f, 1f, 1f, 0f, 0.5f), MaterialFilter.Linear, MaterialAddress.Repeat);
        draftPreviews.SetInputs(new(a, null, -1, recipe));
        Check(draftPreviews.RequestDraft(), "material: explicit draft preview starts");
        var staleDraft = draftPreviews.TakeNext()!;
        draftPreviews.SetInputs(new(a, null, -1, recipe with { Values = recipe.Values with { Red = 0.25f } }));
        draftPreviews.SetInputs(new(a, null, -1, recipe));
        Check(draftPreviews.RequestDraft() && !draftPreviews.CanSend(staleDraft) && !draftPreviews.Settle(staleDraft),
            "material: A to B to A ignores stale draft image and preserves the newer intent");
        var currentDraft = draftPreviews.TakeNext()!;
        draftPreviews.SetInputs(new(a, b));
        Check(!draftPreviews.Settle(currentDraft) && draftPreviews.CanRequest(true),
            "material: closing the modal discards its reply and restores the shared pair preview");

        var preview = new AssetPreviewRequests();
        Check(!preview.Request() && preview.TakeNext() is null, "preview: no visible mesh cannot request");
        preview.SetInputs(new(a, null));
        Check(preview.CanRequest() && preview.TakeNext() is null, "preview: browsing never starts work");
        Check(preview.Request() && !preview.Request(), "preview: one explicit intent per current selection");
        var first = preview.TakeNext()!;
        Check(preview.CanSend(first), "preview: current request admitted after setup");
        preview.SetInputs(new(b, null));
        Check(preview.Request() && preview.TakeNext() is null, "preview: B waits while A is in flight");
        preview.SetInputs(new(a, null));
        Check(preview.Request() && !preview.Settle(first), "preview: late A cannot settle a newer A epoch");
        var latest = preview.TakeNext()!;
        Check(latest.Guid == a && latest.Epoch != first.Epoch && preview.CanSend(latest),
              "preview: A to B to A drops the unsent B and admits only the latest A");
        preview.SetInputs(null);
        Check(!preview.Settle(latest) && !preview.CanRequest() && preview.TakeNext() is null,
              "preview: hiding or deselecting discards late output and unsent work");

        preview.SetInputs(new(a, null));
        Check(preview.Request(), "preview: accepts a new explicit request after navigation");
        var connecting = preview.TakeNext()!;
        preview.SetInputs(null, invalidate: true);
        preview.SetInputs(new(a, null));
        Check(preview.Request() && !preview.CanSend(connecting) && preview.TakeNext() is null,
              "preview: reconnect during setup invalidates the old send and retains one active slot");
        Check(!preview.Settle(connecting), "preview: setup failure from old session cannot change the current view");
        var newSession = preview.TakeNext()!;
        Check(preview.CanSend(newSession) && preview.Settle(newSession) && preview.CanRequest(),
              "preview: timeout on current request releases the slot for an explicit retry");
        Check(preview.Request(), "preview: retry enters the empty slot");
        var retry = preview.TakeNext()!;
        preview.SetInputs(new(a, null), invalidate: true);
        Check(!preview.CanSend(retry) && !preview.Settle(retry) && preview.TakeNext() is null,
              "preview: same-GUID Reload invalidates the request without starting another");

        preview.SetInputs(new(a, b));
        Check(preview.Request(), "pair preview: geometry still explicitly available");
        var geometry = preview.TakeNext()!;
        Check(geometry.Material is null && preview.Request(withMaterial: true) &&
            !preview.Request(withMaterial: true) && preview.TakeNext() is null, "pair preview: mode change replaces the latest intent");
        Check(!preview.Settle(geometry), "pair preview: late geometry cannot replace requested pair");
        var assets = preview.TakeNext()!;
        Check(assets.Material == b && assets.Surface == 0 && preview.CanSend(assets), "pair preview: pair records both roles");
        preview.SetInputs(new(a, b, Surface: 9));
        Check(!preview.CanRequest() && preview.Request(withMaterial: true) && preview.TakeNext() is null,
            "pair preview: chooser shares the active slot and exposes only the pair");
        Check(!preview.CanSend(assets) && !preview.Settle(assets), "pair preview: stale Assets result cannot reach the modal");
        var firstChooser = preview.TakeNext()!;
        preview.SetInputs(null);
        preview.SetInputs(new(a, b, Surface: 10));
        Check(preview.Request(withMaterial: true) && !preview.Settle(firstChooser), "pair preview: same pair after reopening is a new intent");
        var secondChooser = preview.TakeNext()!;
        preview.SetInputs(new(a, a, Surface: 10));
        Check(!preview.CanSend(secondChooser) && !preview.Settle(secondChooser) && preview.TakeNext() is null,
            "pair preview: material-only change cancels pending work without starting a preview");
    }
}
