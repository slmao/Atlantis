using System;
using System.Collections.Generic;
using System.Linq;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Assets;

namespace Atlantis.Editor.Tests;
public static partial class Program
{
    private static void ProjectNavigationChecks()
    {
        var mesh = AssetGuid.Parse("00810081-0081-4081-8081-008100810081");
        var material = AssetGuid.Parse("00810081-0081-4081-8081-008100810082");
        var key = new ProjectSourceKey(ProjectRootKind.Inbox, "storefront.gltf");
        var source = new ProjectSource(key.Path, ProjectSourceKind.Gltf, "storefront", "observed",
            ProjectRegistration.Registered, material, [mesh, material], false);
        ProjectScan Scan(ProjectSource row, ProjectCoverage coverage = ProjectCoverage.Complete, bool ready = true) =>
            new(mesh, false, ready, [new(key.Root, coverage, "", [row])]);
        Check(ProjectNavigation.SourceOf(Scan(source), mesh) == key &&
              ProjectNavigation.SourceOf(Scan(source), material) == key &&
              ProjectNavigation.Matches(Scan(source), key, source, material),
            "project: two exact output identities reveal the same source slot");
        Check(!ProjectNavigation.Matches(Scan(source with { Evidence = "replacement" }), key, source, mesh) &&
              !ProjectNavigation.Matches(Scan(source with { Outputs = [mesh] }), key, source, mesh) &&
              !ProjectNavigation.Matches(Scan(source, ProjectCoverage.Incomplete), key, source, mesh) &&
              ProjectNavigation.SourceOf(Scan(source, ready: false), mesh) is null,
            "project: changed source, changed outputs and uncertain coverage never reveal");
        var browser = new AssetBrowser();
        var a = new AssetSummary(mesh, AssetType.Mesh, "content:store/storefront.gltf#mesh/0/0");
        var b = new AssetSummary(material, AssetType.Material, "content:store_extra/storefront.gltf#material/0");
        browser.CompleteList(browser.BeginList(), Outcome<IReadOnlyList<AssetSummary>, AssetError>.Ok([a, b]));
        browser.Directory = "content:/store";
        browser.Search = "mesh/0/0";
        Check(browser.Visible.Single() == a && browser.Reveal(b.Guid) && browser.Search == "" &&
              browser.Directory == "content:/store_extra" && browser.Visible.Single() == b,
            "catalog: directory boundaries ignore glTF subkeys and reveal clears only blocking filters");
    }

    private static void AssetBrowserChecks()
    {
        AssetBrowseChecks();
        ProjectNavigationChecks();
        AssetDependentChecks();
        AssetGuid a = AssetGuid.Parse("00810081-0081-4081-8081-008100810081"), b = AssetGuid.Parse("00810081-0081-4081-8081-008100810082");
        var provenance = new AssetBrowser();
        var prefab = new AssetSummary(a, AssetType.Scene, "assets:prefabs/prefab_chairs/source.scene.txt") { IsPrefab = true };
        Check(provenance.PrefabSource(a) == PrefabSourceStatus.Pending, "prefab source: an unqueried catalog does not imply absence");
        long firstCatalog = provenance.BeginList(), prefabCatalog = provenance.BeginList();
        Check(provenance.PrefabSource(a) == PrefabSourceStatus.Pending &&
              !provenance.CompleteList(firstCatalog, Outcome<IReadOnlyList<AssetSummary>, AssetError>.Ok([])),
            "prefab source: a late empty response cannot settle a newer lookup");
        provenance.CompleteList(prefabCatalog, Outcome<IReadOnlyList<AssetSummary>, AssetError>.Ok([prefab]));
        provenance.Search = "hidden";
        Check(provenance.PrefabSource(a) == PrefabSourceStatus.Found && provenance.PrefabSource(b) == PrefabSourceStatus.Missing,
            "prefab source: a complete catalog distinguishes present and missing independently of search");
        provenance.CompleteList(provenance.BeginList(), Outcome<IReadOnlyList<AssetSummary>, AssetError>.Err(AssetError.CatalogUnavailable));
        Check(provenance.PrefabSource(a) == PrefabSourceStatus.CatalogUnavailable,
            "prefab source: a query failure does not imply absence");
        var color = new AssetSummary(a, AssetType.Texture, "content:texture_sign/source.png");
        var alternate = new AssetSummary(b, AssetType.Texture, "content:texture_panel/source.png");
        Check(EditorVisuals.AssetName(color) == "sign" && EditorVisuals.AssetName(alternate) == "panel",
            "assets: canonical managed color entries use their distinct registration names");
        Check(EditorVisuals.AssetName(color with { Source = "assets:texture_sign/source.png" }) == "source" &&
              EditorVisuals.AssetName(color with { Source = "content:texture_sign/occlusion.png" }) == "sign",
            "assets: new color label preserves ordinary filenames and managed occlusion labels");
        var renamed = color with { DisplayName = "renamed_blue" };
        var moved = renamed with { LogicalPath = "textures/outside/sign.png" };
        var locations = new AssetBrowser();
        locations.CompleteList(locations.BeginList(), Outcome<IReadOnlyList<AssetSummary>, AssetError>.Ok([moved, alternate]));
        locations.Search = "textures/outside/sign.png";
        Check(locations.Visible.Single() == moved && AssetBrowseProjection.EffectiveLogicalPath(alternate) == "textures/panel.png",
            "move: exact path finds the original asset and unmoved textures have a default path");
        locations.Select(a);
        var move = locations.BeginMove()!;
        locations.Search = "textures/sign.png";
        Check(locations.Visible.Count == 0 && !move.Intent.Current && locations.MoveIntent is null,
            "move: old location is absent and hiding cancels a queued move");
        locations.Search = "texture_sign/source.png";
        Check(locations.Visible.Single().Guid == a && AssetBrowser.RegisteredColorName(moved) == "sign",
            "move: original producer source and inbox token still resolve");
        var names = new AssetBrowser();
        var renamedList = Outcome<IReadOnlyList<AssetSummary>, AssetError>.Ok([renamed, alternate]);
        names.CompleteList(names.BeginList(), renamedList);
        names.Search = "RENAMED_BLUE";
        Check(names.Visible.Single().Guid == a && EditorVisuals.AssetName(names.Visible.Single()) == "renamed_blue" &&
              AssetBrowser.RegisteredColorName(renamed) == "sign", "rename search and original inbox token are independent");
        names.Search = "texture_sign/source.png";
        Check(names.Visible.Single().Guid == a, "rename: old source still searches and resolves to the same GUID");
        names.Select(a);
        var rename = names.BeginRename()!;
        names.Search = "panel";
        Check(!rename.Intent.Current && names.RenameIntent is null, "rename: hiding the target retires the queued intent");
        names.Search = "";
        rename = names.BeginRename()!;
        names.BeginList();
        Check(!rename.Intent.Current, "rename: view reload retires a queued intent even for the same GUID");
        names.CompleteList(names.BeginList(), renamedList);
        var removal = names.BeginRemoval()!;
        names.Select(b);
        Check(!removal.Intent.Current && names.RemovalIntent is null,
            "removal: another selection retires a queued confirm before send");
        removal = names.BeginRemoval()!;
        names.Search = "absent";
        Check(!removal.Intent.Current && names.RemovalIntent is null && names.BeginRemoval() is null,
            "removal: an invisible texture cannot retain or create an intent");
        names.Search = "";
        removal = names.BeginRemoval()!;
        names.BeginList();
        Check(!removal.Intent.Current, "removal: Reload view rejects late receipts for the unchanged GUID");

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
        Check(browser.BeginDependents() is not null && AssetBrowser.RegisteredColorName(texture) == "probe" &&
            AssetBrowser.RegisteredColorName(texture with { Source = "content:texture_probe/source.png#subkey" }) is null &&
            AssetBrowser.RegisteredColorName(texture with { Source = "content:texture_../source.png" }) is null,
            "references: linear textures can be queried while reimport still requires a canonical color source");
        browser.Select(texture.Guid);
        var scan = browser.BeginDependents()!;
        Check(scan.Candidates.SequenceEqual(new[] { early, late }) && browser.BeginDependents() is null,
            "reimport: one scan, material rows only, deterministic source order");
        browser.Group = AssetBrowseGroup.Source;
        var baseRow = new AssetReferenceRow(early, TextureReferenceSlot.BaseColor);
        var emissiveRow = new AssetReferenceRow(early, TextureReferenceSlot.Emissive);
        var unavailable = new AssetReferenceRow(late, null, "Unsupported");
        Check(scan.Intent.Current && browser.CompleteDependents(scan, new(new[] { baseRow, emissiveRow, unavailable }, false)) &&
            browser.DependentResult is { Complete: false } partial && partial.Matches.SequenceEqual(new[] { baseRow, emissiveRow, unavailable }),
            "reimport: grouping preserves the current scan and an incomplete result retains verified matches");
        Check(browser.BeginReferenceLocate(unavailable, null) is null,
            "references: unresolved edges cannot be located");
        var values = new MaterialValues(texture.Guid, 1, 1, 1, 0, 1) { EmissiveTexture = texture.Guid };
        var material = new MaterialRead(early.Guid, new(values, MaterialFilter.Linear, MaterialAddress.Repeat), false, null);
        var detail = new AssetDetails(early.Guid, AssetType.Material, early.Source, 9, 10, "test", new[] { texture.Guid });
        Check(MaterialTextureReferences.MatchesDetail(early, detail) && MaterialTextureReferences.ProvesSlots(detail, material, null) &&
            MaterialTextureReferences.Slots(values).Where(p => p.Texture == texture.Guid).Select(p => p.Slot)
                .SequenceEqual(new[] { TextureReferenceSlot.BaseColor, TextureReferenceSlot.Emissive }),
            "references: one dependency GUID proves two independent slots on a read-only material");
        var allMaps = values with { NormalMap = linear.Guid, MetallicRoughnessMap = late.Guid, OcclusionMap = early.Guid };
        Check(MaterialTextureReferences.Slots(allMaps).Select(p => p.Texture).SequenceEqual(
                new AssetGuid?[] { texture.Guid, linear.Guid, late.Guid, early.Guid, texture.Guid }) &&
            !MaterialTextureReferences.ProvesSlots(detail, material with { Recipe = material.Recipe with { Values = allMaps } }, null) &&
            !MaterialTextureReferences.ProvesSlots(detail, material with { ActiveGeneration = new(late.Guid) }, new GenerationId(early.Guid)) &&
            !MaterialTextureReferences.MatchesDetail(early, detail with { Type = AssetType.Texture }),
            "references: five ordered slots, mismatched dependencies, generation and detail identity");
        browser.Search = texture.Guid.ToString();
        browser.Type = AssetType.Texture;
        var first = browser.BeginReferenceLocate(baseRow, null)!;
        var second = browser.BeginReferenceLocate(emissiveRow, null)!;
        Check(!first.Intent.Current && !browser.CompleteReferenceLocate(first, material, out _),
            "references: newer locate retires a pending reply");
        var changed = material with { Recipe = material.Recipe with { Values = values with { EmissiveTexture = null } } };
        Check(browser.CompleteReferenceLocate(second, changed, out var refusal) && refusal is null &&
            browser.Selected == texture.Guid && browser.Type == AssetType.Texture && browser.Search == texture.Guid.ToString() &&
            browser.ReferenceNotice?.Contains("Refresh references") == true,
            "references: a surviving Base Color edge cannot validate the former Emissive slot");
        Check(browser.BeginReferenceLocate(emissiveRow, null) is null, "references: a failed locate requires refresh");
        scan = browser.BeginDependents()!;
        browser.CompleteDependents(scan, new(new[] { baseRow, emissiveRow }, true));
        second = browser.BeginReferenceLocate(emissiveRow, null)!;
        Check(browser.CompleteReferenceLocate(second, material, out var located) && located?.Slot == TextureReferenceSlot.Emissive &&
            browser.Reveal(early.Guid) && browser.Search == "" && browser.Type is null && browser.Group == AssetBrowseGroup.Source,
            "references: exact guid reveal clears only excluding filters and preserves grouping");
        var detailRequest = browser.Select(early.Guid, located);
        Check(browser.CompleteDetail(detailRequest, early.Guid, Outcome<AssetDetails, AssetError>.Ok(detail)) &&
            ReferenceEquals(browser.LocatedReference, located), "references: generic detail completion preserves the located slot");
        browser.Select(texture.Guid);
        Check(browser.LocatedReference is null && browser.BeginReferenceLocate(emissiveRow, null) is null,
            "references: row and landing cannot revive after selecting away and back");
        scan = browser.BeginDependents()!;
        browser.Search = "hidden";
        browser.Search = "";
        Check(!scan.Intent.Current && !browser.CompleteDependents(scan, new(Array.Empty<AssetReferenceRow>(), true)),
            "reimport: hide/show cannot revive an obsolete dependent scan");
        scan = browser.BeginDependents()!;
        browser.CompleteList(browser.BeginList(), snapshot);
        Check(!scan.Intent.Current && browser.DependentResult is null, "reimport: catalog reload cancels dependent feedback");
        scan = browser.BeginDependents()!;
        browser.CancelPendingDependents();
        Check(!scan.Intent.Current && !browser.CompleteDependents(scan, new(Array.Empty<AssetReferenceRow>(), true)),
            "reimport: leaving the Assets pane suppresses pending dependent feedback");
        scan = browser.BeginDependents()!;
        browser.Disconnect();
        Check(!scan.Intent.Current && !browser.CompleteDependents(scan, new(Array.Empty<AssetReferenceRow>(), true)),
            "reimport: disconnect suppresses late dependent feedback");
        browser.CompleteList(browser.BeginList(), Outcome<IReadOnlyList<AssetSummary>, AssetError>.Ok(new[] { texture, early, early, late }));
        browser.Select(texture.Guid);
        scan = browser.BeginDependents()!;
        Check(!scan.RosterComplete && scan.Candidates.SequenceEqual(new[] { late }), "references: duplicate identities make the roster incomplete");
        browser.CompleteDependents(scan, new(new[] { unavailable }, false));
        browser.Search = texture.Guid.ToString();
        Check(browser.InterruptReferences() && browser.Selected == texture.Guid && browser.Search == texture.Guid.ToString() &&
            browser.DependentResult is { Complete: false, Matches.Count: 1 },
            "references: transport failure retains the texture and known rows as incomplete");
        browser.CompleteList(browser.BeginList(), Outcome<IReadOnlyList<AssetSummary>, AssetError>.Err(AssetError.CatalogUnavailable));
        Check(browser.State == "CatalogUnavailable" && browser.DependentResult is null && browser.BeginDependents() is null,
            "references: unavailable catalog does not produce a complete empty result");
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
