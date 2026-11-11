using System;
using System.Collections.Generic;
using System.Linq;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Assets;
using Atlantis.Gameplay.Authoring;

namespace Atlantis.Editor.Model;

/// <summary>Owned launch values, safe to pass from the session worker to the UI.</summary>
public sealed record AssetReferenceTarget(long Request, SceneAddress Address, string Path, string Label,
                                          PropertyValue Before, AssetType? TargetAssetType, AssetGuid? OpeningMesh)
{
    public bool SupportsMaterialPreview => Path == "Renderable.materialAsset";
}

/// <summary>Worker-thread-only target guard. Invalidations are sticky; these local
/// request states are not wire revisions or atomic conditional edits.</summary>
public sealed class AssetReferencePicker
{
    private AssetReferencePicker(AssetReferenceTarget target) => Target = target;
    public AssetReferenceTarget Target { get; }
    public string? Refusal { get; private set; }
    public bool Submitted { get; private set; }
    private long _listRequest;
    private HashSet<AssetGuid>? _eligible;

    public static AssetReferencePicker? Capture(long request, EntityGuid node, InspectorField field,
                                                IReadOnlyList<InspectorField> openingFields)
    {
        if (!InspectorModel.CanChooseAsset(field)) return null;
        AssetGuid? openingMesh = null;
        if (field.Path == "Renderable.materialAsset" &&
            openingFields.FirstOrDefault(f => f.Component == field.Component && f.Path == "Renderable.meshAsset")?.Value
                is PropertyValue value && value.TryGetAsset(out AssetGuid mesh) && !mesh.IsNil)
            openingMesh = mesh;
        return new(new(request, new SceneAddress(node, field.Component, field.Field.Id), field.Path, field.Label,
                       field.Value!.Value, field.Field.TargetAssetType, openingMesh));
    }

    public bool BeginList(long listRequest)
    {
        if (Refusal is not null || Submitted || listRequest <= _listRequest) return false;
        _listRequest = listRequest;
        _eligible = null;
        return true;
    }

    public void CompleteList(long listRequest, Outcome<IReadOnlyList<AssetSummary>, AssetError> result)
    {
        if (Refusal is not null || Submitted || listRequest != _listRequest) return;
        if (Target.TargetAssetType is AssetType type && result.IsOk)
            _eligible = result.Value.Where(asset => asset.Type == type).Select(asset => asset.Guid).ToHashSet();
    }

    public void Invalidate(string reason)
    {
        Refusal ??= reason;
        _eligible = null;
    }

    public void SelectionChanged(EntityGuid? selection)
    { if (selection != Target.Address.Node) Invalidate("the selection changed"); }

    public void Observe(Event e)
    {
        if (e is DocumentReopened) Invalidate("the document was reopened");
        else if (e is NodeDestroyed gone && gone.Node == Target.Address.Node) Invalidate("the target was deleted");
        else if (e is ComponentRemoved removed && removed.Node == Target.Address.Node && removed.Component == Target.Address.Component)
            Invalidate("the target component was removed");
        else if (e is PropertyChanged changed && changed.Address == Target.Address && !changed.Value.Equals(Target.Before))
            Invalidate("the target changed elsewhere");
    }

    public EditPlan Confirm(InspectorModel inspector, EntityGuid? selection, AssetGuid candidate, long listRequest = 0)
    {
        const string label = "choose asset";
        if (Submitted) return EditPlan.Refused(label, "the choice was already submitted");
        Submitted = true;
        SelectionChanged(selection);
        if (Refusal is string reason) return EditPlan.Refused(label, reason);
        InspectorField? field = inspector.Fields.FirstOrDefault(f => f.Component == Target.Address.Component && f.Field.Id == Target.Address.Field);
        if (inspector.Subject != Target.Address.Node || field is null || !InspectorModel.CanChooseAsset(field) ||
            field.Field.TargetAssetType != Target.TargetAssetType || !field.Value!.Value.Equals(Target.Before))
        {
            Invalidate("the target changed or cannot be read");
            return EditPlan.Refused(label, Refusal!);
        }
        if (Target.TargetAssetType is not null &&
            (listRequest != _listRequest || _eligible?.Contains(candidate) != true))
            return EditPlan.Refused(label, "the asset is not eligible in the displayed list");
        return inspector.Commit(field.Path, candidate.ToString());
    }
}
