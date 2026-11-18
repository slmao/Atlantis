using System;
using System.Linq;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Authoring;
using Atlantis.Gameplay.Assets;

namespace Atlantis.Editor.Tests;

public static partial class Program
{
    private static void AssetDragDropChecks()
    {
        var mesh = new AssetSummary(AssetGuid.Parse("00820082-0082-4082-8082-008200820003"), AssetType.Mesh, "assets:mesh");
        AssetDragIntent Drag() => new(mesh, new object(), 5);
        var drag = Drag();
        Check(!drag.Matches(mesh.Guid.ToString()) && !Drag().Matches(drag.Token), "asset drag: payload is local and opaque");
        Check(drag.Matches(drag.Token) && drag.Queue() && !drag.Queue() && !drag.Matches(drag.Token), "asset drag: once-only drop");
        drag.Cancel(onlyUndropped: true);
        Check(drag.Send(() => 17) == 17 && drag.Send(() => throw new Exception("duplicate send")) is null,
            "asset drag: native completion preserves queue, one send");
        drag = Drag();
        drag.Queue();
        drag.Cancel();
        Check(drag.Send(() => throw new Exception("cancelled send")) is null, "asset drag: queued drop invalidation prevents send");
        drag = Drag();
        drag.Cancel(onlyUndropped: true);
        Check(!drag.Queue(), "asset drag: undropped completion expires token");

        var doc = new PickerDocument { TargetType = AssetType.Material, Value = PropertyValue.Absent };
        var tree = new DocumentTree();
        tree.Rebuild(doc);
        var inspector = new InspectorModel(doc);
        inspector.Refresh(doc.Node);
        var picker = AssetReferencePicker.Capture(1, doc.Node, inspector.Fields.Single(), inspector.Fields)!;
        var address = picker.Target.Address;
        var field = new AssetDropField(picker.Target, new BatchPropertyTarget(address, doc.Value,
            tree.IncarnationOf(doc.Node), tree.ComponentIncarnationOf(address), tree.PropertyStamp(address)), 8, 5);

        Check(field.Accepts(AssetType.Material) && !field.Accepts(mesh.Type) &&
            (field with { Field = field.Field with { TargetAssetType = null } }).Accepts(mesh.Type),
            "asset drag: typed reference and generic fallback");
        Check(field.IsCurrent(tree, 8, 5) && !field.IsCurrent(tree, 10, 5) && !field.IsCurrent(tree, 8, 6),
            "asset drag: away/back selection and reopen expire display");
        tree.Apply(new Atlantis.Gameplay.Authoring.PropertyChanged(address, PropertyValue.FromAsset(mesh.Guid)), doc);
        tree.Apply(new Atlantis.Gameplay.Authoring.PropertyChanged(address, PropertyValue.Absent), doc);
        Check(!field.IsCurrent(tree, 8, 5) && field.IsCurrent(tree, 8, 5, checkStamp: false),
            "asset drag: observed change then restore expires displayed field");
        tree.Apply(new Atlantis.Gameplay.Authoring.ComponentRemoved(doc.Node, doc.Component), doc);
        tree.Apply(new Atlantis.Gameplay.Authoring.ComponentAdded(doc.Node, doc.Component), doc);
        Check(!field.IsCurrent(tree, 8, 5, checkStamp: false), "asset drag: same component type is a new occurrence");
        tree.Apply(new NodeDestroyed(doc.Node), doc);
        tree.Apply(new NodeCreated(doc.Node, 0), doc);
        Check(!field.IsCurrent(tree, 8, 5, checkStamp: false), "asset drag: same GUID is a new occurrence");
    }
}
