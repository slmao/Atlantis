using System;
using System.Collections.Generic;
using System.Linq;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Assets;

namespace Atlantis.Editor.Tests;
public static partial class Program
{
    private static void AssetBrowserChecks()
    {
        AssetBrowseChecks();
        AssetDependentChecks();
        AssetGuid a = AssetGuid.Parse("00810081-0081-4081-8081-008100810081"), b = AssetGuid.Parse("00810081-0081-4081-8081-008100810082");
        var color = new AssetSummary(a, AssetType.Texture, "content:texture_sign/source.png");
        var alternate = new AssetSummary(b, AssetType.Texture, "content:texture_panel/source.png");
        Check(EditorVisuals.AssetName(color) == "sign" && EditorVisuals.AssetName(alternate) == "panel",
            "assets: canonical managed color entries use their distinct registration names");
        Check(EditorVisuals.AssetName(color with { Source = "assets:texture_sign/source.png" }) == "source" &&
              EditorVisuals.AssetName(color with { Source = "content:texture_sign/occlusion.png" }) == "sign",
            "assets: new color label preserves ordinary filenames and managed occlusion labels");

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
        var resume = new MaterialTarget(MaterialMode.Resume, Material: b);
        settlement.Begin(resume);
        settlement.Refused();
        Check(settlement.Job == failed && settlement.Target == target && settlement.Id == receipt.Id &&
            settlement.BlocksSave && settlement.ResumableMaterial == b,
            "material: a definitive Resume refusal preserves the committed receipt and keeps Save blocked");
        settlement.Begin(resume);
        var resumeReceipt = new MaterialJob(new(b), resume, RebuildState.Running);
        Check(settlement.Admit(resumeReceipt), "material: Resume admission is tracked separately");
        var resumeRefusal = resumeReceipt with { State = RebuildState.Failed, Failure = new(MaterialStage.Source,
            MaterialFailureCode.ActivationRequired, "another change is ahead", null, RegistrationState.NotCommitted, null) };
        Check(settlement.Observe(new(null, resumeRefusal, false)) && settlement.Job == resumeRefusal &&
            settlement.BlocksSave && settlement.ResumableMaterial == b,
            "material: failed Resume reports the new diagnostic without discarding the committed source state");
        settlement.Begin(resume);
        settlement.LoseReceipt();
        settlement.Refused();
        Check(settlement.Unknown && settlement.Target == resume && settlement.BlocksSave,
            "material: losing Resume admission remains unknown even with a prior committed receipt");

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

        var mapped = recipe with { Values = recipe.Values with { NormalMap = a, EmissiveTexture = b, EmissiveRed = 0.5f } };
        draftPreviews.SetInputs(new(a, null, 8, mapped, true));
        Check(draftPreviews.RequestDraft(), "material maps: explicit preview admits a full recipe");
        var staleMapped = draftPreviews.TakeNext()!;
        var cleared = mapped with { Values = mapped.Values with { NormalMap = null, EmissiveTexture = null } };
        draftPreviews.SetInputs(new(a, null, 8, cleared, true));
        Check(!draftPreviews.Settle(staleMapped) && draftPreviews.TakeNext() is null,
            "material maps: Clear rejects a late reply without requesting another preview");
        draftPreviews.SetInputs(new(a, null, 9, mapped, true));
        Check(draftPreviews.RequestDraft(), "material maps: reopened modal can request the same recipe explicitly");
        var reopenedMapped = draftPreviews.TakeNext()!;
        Check(!draftPreviews.Settle(staleMapped) && draftPreviews.Settle(reopenedMapped),
            "material maps: only the reopened modal can settle its preview");

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

    private static void AssetDependentChecks()
    {
        var texture = new AssetSummary(AssetGuid.Parse("01160116-0001-4116-8116-011601160001"), AssetType.Texture,
            "content:texture_probe/source.png");
        var late = new AssetSummary(AssetGuid.Parse("01160116-0002-4116-8116-011601160002"), AssetType.Material,
            "assets:materials/z.material.txt");
        var early = new AssetSummary(AssetGuid.Parse("01160116-0003-4116-8116-011601160003"), AssetType.Material,
            "assets:materials/a.material.txt");
        var linear = new AssetSummary(AssetGuid.Parse("01160116-0004-4116-8116-011601160004"), AssetType.Texture,
            "content:texture_normal/normal.png");
        var browser = new AssetBrowser();
        var snapshot = Outcome<IReadOnlyList<AssetSummary>, AssetError>.Ok(new[] { texture, late, early, linear });
        browser.CompleteList(browser.BeginList(), snapshot);
        browser.Select(linear.Guid);
        Check(browser.BeginDependents() is null && AssetBrowser.RegisteredColorName(texture) == "probe" &&
            AssetBrowser.RegisteredColorName(texture with { Source = "content:texture_probe/source.png#subkey" }) is null &&
            AssetBrowser.RegisteredColorName(texture with { Source = "content:texture_../source.png" }) is null,
            "reimport: only a canonical registered color source is a provisional target");
        browser.Select(texture.Guid);
        var scan = browser.BeginDependents()!;
        Check(scan.Candidates.SequenceEqual(new[] { early, late }) && browser.BeginDependents() is null,
            "reimport: one scan, material rows only, deterministic source order");
        browser.Group = AssetBrowseGroup.Source;
        Check(scan.Intent.Current && browser.CompleteDependents(scan, new(new[] { early }, false)) &&
            browser.DependentResult is { Complete: false } partial && partial.Matches.SequenceEqual(new[] { early }),
            "reimport: grouping preserves the current scan and an incomplete result retains verified matches");
        scan = browser.BeginDependents()!;
        browser.Search = "hidden";
        browser.Search = "";
        Check(!scan.Intent.Current && !browser.CompleteDependents(scan, new(Array.Empty<AssetSummary>(), true)),
            "reimport: hide/show cannot revive an obsolete dependent scan");
        scan = browser.BeginDependents()!;
        browser.CompleteList(browser.BeginList(), snapshot);
        Check(!scan.Intent.Current && browser.DependentResult is null, "reimport: catalog reload cancels dependent feedback");
        scan = browser.BeginDependents()!;
        browser.CancelPendingDependents();
        Check(!scan.Intent.Current && !browser.CompleteDependents(scan, new(Array.Empty<AssetSummary>(), true)),
            "reimport: leaving the Assets pane suppresses pending dependent feedback");
        scan = browser.BeginDependents()!;
        browser.Disconnect();
        Check(!scan.Intent.Current && !browser.CompleteDependents(scan, new(Array.Empty<AssetSummary>(), true)),
            "reimport: disconnect suppresses late dependent feedback");
    }

    private static void AssetBrowseChecks()
    {
        AssetGuid Id(int n) => new(new Guid(n, 114, 4114, new byte[] { 0x81, 0x14, 0, 0, 0, 0, 0, 1 }));
        var texture = new AssetSummary(Id(1), AssetType.Texture, "content:texture_studio/source.png");
        var sameA = new AssetSummary(Id(2), AssetType.Material, "assets:a/same.material.txt");
        var sameB = new AssetSummary(Id(3), AssetType.Material, "content:b/same.material.txt");
        var imported = new AssetSummary(Id(4), AssetType.Mesh, "content:room/scene.gltf#mesh/2");
        var other = new AssetSummary(Id(5), AssetType.Scene, "future:world.scene.txt");
        var values = new[] { other, sameB, texture, imported, sameA };
        var browser = new AssetBrowser();
        browser.CompleteList(browser.BeginList(), Outcome<IReadOnlyList<AssetSummary>, AssetError>.Ok(values));
        browser.Search = " STUDIO ";
        Check(browser.Visible.Single() == texture, "browse: displayed managed name matches a trimmed query");
        browser.Search = "room" + (char)92 + "scene.gltf";
        Check(browser.Visible.Single() == imported, "browse: Windows path separator matches logical source");
        browser.Search = "mesh/2";
        Check(browser.Visible.Single() == imported, "browse: imported sub-key remains searchable");
        browser.Search = Id(3).Value.ToString("N").ToUpperInvariant();
        Check(browser.Visible.Single() == sameB, "browse: compact canonical GUID matches");
        browser.Type = AssetType.Texture;
        Check(browser.Visible.Count == 0 && browser.ViewState == "No matching assets", "browse: type intersects GUID search");
        browser.Type = null; browser.Search = "";
        var beforeRows = browser.Rows;
        browser.Select(sameA.Guid);
        var intent = browser.DetailIntent!;
        long detailRequest = browser.Select(sameA.Guid);
        var detail = new AssetDetails(sameA.Guid, sameA.Type, sameA.Source, 7, null, "test", []);
        Check(!intent.Current && browser.CompleteDetail(detailRequest, sameA.Guid, Outcome<AssetDetails, AssetError>.Ok(detail)),
            "browse: replacing a queued detail cancels its old intent");
        browser.Sort = AssetBrowseSort.NameDescending;
        Check(browser.Details == detail && browser.DetailIntent!.Current &&
            browser.Visible.Where(asset => AssetBrowseProjection.AssetName(asset) == "same").SequenceEqual(new[] { sameA, sameB }),
            "browse: descending name sort keeps ascending tie breakers and selected details");
        browser.Group = AssetBrowseGroup.Type;
        Check(browser.Rows.OfType<AssetGroupHeader>().Select(h => h.Label).SequenceEqual(new[] { "Mesh", "Texture", "Material", "Scene" }) &&
            browser.Rows.OfType<AssetGroupHeader>().Sum(h => h.Count) == values.Length,
            "browse: type headers have fixed order and exact counts");
        browser.Group = AssetBrowseGroup.Source;
        Check(browser.Rows.OfType<AssetGroupHeader>().Select(h => h.Label).SequenceEqual(new[] { "assets:", "content:", "Other" }) &&
            browser.Details == detail, "browse: root groups include unknown sources without losing details");
        var stableRows = browser.Rows;
        browser.CompleteDetail(detailRequest, sameA.Guid, Outcome<AssetDetails, AssetError>.Ok(detail));
        Check(ReferenceEquals(stableRows, browser.Rows), "browse: detail replies keep the result surface");
        browser.Search = "studio";
        Check(browser.Selected == sameA.Guid && !browser.SelectionVisible && browser.Details is null &&
            !browser.CompleteDetail(detailRequest, sameA.Guid, Outcome<AssetDetails, AssetError>.Ok(detail)),
            "browse: hidden selection keeps identity while rejecting the old detail");
        browser.Search = "";
        Check(browser.NeedsDetail && browser.Selected == sameA.Guid, "browse: reveal requests one fresh detail");
        long refreshed = browser.Select(sameA.Guid);
        Check(browser.DetailState == "Loading" && !browser.NeedsDetail &&
            !browser.CompleteDetail(detailRequest, sameA.Guid, Outcome<AssetDetails, AssetError>.Ok(detail)),
            "browse: A back to A never accepts the previous callback");
        browser.Disconnect();
        Check(browser.Selected is null && browser.Rows.Count == 0 && browser.Items.Count == 0 &&
            !browser.CompleteDetail(refreshed, sameA.Guid, Outcome<AssetDetails, AssetError>.Ok(detail)),
            "browse: reconnect cannot inherit selection, a row scroll anchor or detail");

        var longList = Enumerable.Range(1, 10000).Select(n => new AssetSummary(Id(n), (AssetType)(n % 5),
            (n % 2 == 0 ? "assets:" : "content:") + "long/item_" + n.ToString("D5") + ".png")).Reverse().ToArray();
        browser.CompleteList(browser.BeginList(), Outcome<IReadOnlyList<AssetSummary>, AssetError>.Ok(longList));
        browser.Group = AssetBrowseGroup.None; browser.Sort = AssetBrowseSort.NameAscending;
        Check(browser.Visible.Count == 10000 && browser.Visible[0].Guid == Id(1) && browser.Visible[^1].Guid == Id(10000),
            "browse: long catalog has deterministic full membership and name order");
        browser.Group = AssetBrowseGroup.Type;
        Check(browser.Rows.OfType<AssetGroupHeader>().Count() == 5 &&
            browser.Rows.OfType<AssetSummary>().Select(asset => asset.Guid).Distinct().Count() == 10000,
            "browse: grouped long catalog retains every GUID once");
    }
}
