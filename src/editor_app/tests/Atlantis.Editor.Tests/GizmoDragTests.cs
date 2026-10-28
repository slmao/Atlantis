// Plan 0068 M4 (Spec 0068 R5, R7; Plan J3 and J4 as ruled; ADR-0135 D1, D2): the
// Gizmo drag controller against the fake document and a recording preview --
// previews of the node and its descendants (each keeping its placement
// relative to the node), the 256-descendant cap, the release as one
// SetProperty of the mode's field whose result reproduces the previewed world
// matrix, and the refusals (a result that would shear under a non-uniformly
// scaled parent, an unchanged drag) that revert the preview.
using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Authoring;
using Command = Atlantis.Gameplay.Authoring.Command;
using SetProperty = Atlantis.Gameplay.Authoring.SetProperty;
using PropertyChanged = Atlantis.Gameplay.Authoring.PropertyChanged;

namespace Atlantis.Editor.Tests;

public static partial class Program
{
    private sealed class RecordingPreview : IWorldPreview
    {
        public readonly List<IReadOnlyList<(EntityGuid Node, float[] World)>> Batches = new();

        public void Write(IReadOnlyList<(EntityGuid Node, float[] World)> matrices) => Batches.Add(matrices);

        public float[] Last(EntityGuid node) => Batches[^1].First(w => w.Node == node).World;
    }

    private static bool Near16(float[] a, float[] b, float tolerance) => NearMatrix(a, b, tolerance);

    private static void GizmoDragChecks()
    {
        // A root R at (1, 2, 3) with children A (offset 2 on x) and B under A (offset 1 on y).
        var document = new FakeDocument();
        EntityGuid root = document.Add(1, null, Local(new(1, 2, 3), Vector3.Zero, Vector3.One));
        EntityGuid a = document.Add(2, root, Local(new(2, 0, 0), Vector3.Zero, Vector3.One));
        EntityGuid b = document.Add(3, a, Local(new(0, 1, 0), Vector3.Zero, Vector3.One));
        var tree = new DocumentTree();
        tree.Rebuild(document);
        var transforms = new LocalTransforms();
        var preview = new RecordingPreview();
        var drag = new GizmoDragController(document, tree, transforms, preview);
        var selection = new Selection();

        Check(drag.Availability(root, GizmoSpace.World) == new GizmoAvailability(true, true, true, null), "gizmo drag: a plain node allows every mode");
        Check(drag.Begin(root, GizmoMode.Translate, GizmoSpace.World, GizmoAxis.X, snap: false) && drag.Dragging && drag.PreviewedDescendants == 2,
              "gizmo drag: begins on the node, previewing its two descendants");
        float[] start = TransformMath.WorldOf(document, tree, root);
        float[] moved = drag.Preview(1.5f)!;
        Check(preview.Batches.Count == 1 && preview.Batches[0].Count == 3 && Near16(moved, WorldMatrices.Multiply(Translate(1.5f, 0, 0), start), 1e-6f),
              "gizmo drag: a batch writes the node and both descendants, the node moved by the amount");
        Check(Near16(preview.Last(a), WorldMatrices.Multiply(Translate(1.5f, 0, 0), TransformMath.WorldOf(document, tree, a)), 1e-6f) &&
              Near16(preview.Last(b), WorldMatrices.Multiply(Translate(1.5f, 0, 0), TransformMath.WorldOf(document, tree, b)), 1e-6f),
              "gizmo drag: each descendant keeps its placement relative to the node");
        drag.Preview(2.0f);
        EditPlan plan = drag.Release();
        Check(plan.Refusal is null && plan.Label == "move" && plan.Commands.Count == 1 && plan.Commands[0] is SetProperty { Address.Field: var field } && field == FakeDocument.Position,
              "gizmo drag: the release is one SetProperty of localPosition");
        int writesBefore = document.Submissions;
        Check(Submit(document, tree, selection, plan) is null && document.Submissions == writesBefore + 1 && !drag.Dragging, "gizmo drag: the document accepts it, the drag has ended");
        transforms.Apply(new PropertyChanged(new SceneAddress(root, FakeDocument.Transform, FakeDocument.Position), PropertyValue.FromVec3(Vector3.Zero)));
        Check(Near16(TransformMath.WorldOf(document, tree, root), WorldMatrices.Multiply(Translate(2.0f, 0, 0), start), 1e-6f) &&
              Near16(TransformMath.WorldOf(document, tree, b), preview.Last(b), 1e-5f),
              "gizmo drag: the committed world matrices equal the last preview, the descendants' included");

        // Under a rotated parent a world-axis drag still moves the node along the world's axis.
        var d2 = new FakeDocument();
        EntityGuid p2 = d2.Add(1, null, Local(new(5, 0, 0), new(0.3f, 0.9f, -0.2f), new(2, 2, 2)));
        EntityGuid c2 = d2.Add(2, p2, Local(new(1, 1, 0), new(0.1f, 0.0f, 0.4f), new(1, 3, 1)));
        var tree2 = new DocumentTree();
        tree2.Rebuild(d2);
        var transforms2 = new LocalTransforms();
        var drag2 = new GizmoDragController(d2, tree2, transforms2, new RecordingPreview());
        float[] childStart = TransformMath.WorldOf(d2, tree2, c2);
        bool began = drag2.Begin(c2, GizmoMode.Translate, GizmoSpace.World, GizmoAxis.Y, snap: false);
        drag2.Preview(0.75f);
        EditPlan under = drag2.Release();
        Check(began && under.Refusal is null && Submit(d2, tree2, new Selection(), under) is null &&
              Near16(TransformMath.WorldOf(d2, tree2, c2), WorldMatrices.Multiply(Translate(0, 0.75f, 0), childStart), 1e-5f),
              "gizmo drag: under a rotated, uniformly scaled parent the node moves exactly along the world axis");

        // Rotate and scale write only their own field, and the result reproduces the previewed matrix.
        var d3 = new FakeDocument();
        EntityGuid r3 = d3.Add(1, null, Local(new(1, 1, 1), new(0.2f, 0.4f, 0.0f), new(1, 2, 1)));
        var tree3 = new DocumentTree();
        tree3.Rebuild(d3);
        var drag3 = new GizmoDragController(d3, tree3, new LocalTransforms(), new RecordingPreview());
        drag3.Begin(r3, GizmoMode.Rotate, GizmoSpace.World, GizmoAxis.Y, snap: false);
        float[] rotated = drag3.Preview(0.5f)!;
        EditPlan rot = drag3.Release();
        Check(rot.Refusal is null && rot.Label == "rotate" && ((SetProperty)rot.Commands[0]).Address.Field == FakeDocument.Euler &&
              Submit(d3, tree3, new Selection(), rot) is null && Near16(TransformMath.WorldOf(d3, tree3, r3), rotated, 1e-4f),
              "gizmo drag: a rotate writes only the Euler angles and reproduces the previewed matrix");
        var drag3b = new GizmoDragController(d3, tree3, new LocalTransforms(), new RecordingPreview());
        drag3b.Begin(r3, GizmoMode.Scale, GizmoSpace.Local, GizmoAxis.X, snap: false);
        float[] scaled = drag3b.Preview(1.5f)!;
        EditPlan sc = drag3b.Release();
        Check(sc.Refusal is null && sc.Label == "scale" && ((SetProperty)sc.Commands[0]).Address.Field == FakeDocument.ScaleField &&
              Submit(d3, tree3, new Selection(), sc) is null && Near16(TransformMath.WorldOf(d3, tree3, r3), scaled, 1e-4f),
              "gizmo drag: a scale writes only the scale and reproduces the previewed matrix");

        // An unchanged drag is refused and reverts.
        var pv = new RecordingPreview();
        var drag4 = new GizmoDragController(d3, tree3, new LocalTransforms(), pv);
        float[] before4 = TransformMath.WorldOf(d3, tree3, r3);
        drag4.Begin(r3, GizmoMode.Translate, GizmoSpace.World, GizmoAxis.Z, snap: false);
        drag4.Preview(0.0f);
        EditPlan same = drag4.Release();
        Check(same.Refusal == "unchanged" && !drag4.Dragging && Near16(pv.Last(r3), before4, 0), "gizmo drag: a drag that ends where it began is refused and reverted");

        // Snapping rounds the amount (translate 0.5, from the start).
        var drag5 = new GizmoDragController(d3, tree3, new LocalTransforms(), new RecordingPreview());
        float[] s5 = TransformMath.WorldOf(d3, tree3, r3);
        drag5.Begin(r3, GizmoMode.Translate, GizmoSpace.World, GizmoAxis.X, snap: true);
        Check(Near16(drag5.Preview(0.7f)!, WorldMatrices.Multiply(Translate(0.5f, 0, 0), s5), 1e-6f), "gizmo drag: snapping rounds a translate to 0.5");
        drag5.Revert();

        // J3: 256 descendants preview with the node; 257 only the node.
        foreach (int count in new[] { 256, 257 })
        {
            var big = new FakeDocument();
            EntityGuid top = big.Add(1, null, LocalTransform.Identity);
            EntityGuid last = top;
            for (int i = 0; i < count; ++i) last = big.Add(2 + i, last, Local(new(0, 0.01f, 0), Vector3.Zero, Vector3.One));
            var bigTree = new DocumentTree();
            bigTree.Rebuild(big);
            var bigPreview = new RecordingPreview();
            var bigDrag = new GizmoDragController(big, bigTree, new LocalTransforms(), bigPreview);
            bigDrag.Begin(top, GizmoMode.Translate, GizmoSpace.World, GizmoAxis.X, snap: false);
            bigDrag.Preview(1.0f);
            Check(bigPreview.Batches[0].Count == (count == 256 ? 257 : 1) && bigDrag.PreviewedDescendants == (count == 256 ? 256 : 0),
                  $"gizmo drag (J3): a subtree of {count} descendants previews {(count == 256 ? "the node and all of them" : "only the node")}");
            bigDrag.Revert();
        }

        // J4's rule on a release: under a parent scaled (1, 1, 0.25) and rotated, a rotate would shear the child.
        var d6 = new FakeDocument();
        EntityGuid squashed = d6.Add(1, null, Local(new(2, 0, -1), new(0.3f, 0.5f, 0.2f), new(1, 1, 0.25f)));
        EntityGuid kid = d6.Add(2, squashed, LocalTransform.Identity);
        var tree6 = new DocumentTree();
        tree6.Rebuild(d6);
        var pv6 = new RecordingPreview();
        var drag6 = new GizmoDragController(d6, tree6, new LocalTransforms(), pv6);
        GizmoAvailability availability = drag6.Availability(kid, GizmoSpace.World);
        Check(availability.Translate && !availability.Rotate && availability.Reason!.Contains("non-uniformly scaled parent", StringComparison.Ordinal),
              "gizmo drag (J4): under a squashed, rotated parent, rotate is unavailable with its reason and translate stays");
        float[] kidStart = TransformMath.WorldOf(d6, tree6, kid);
        drag6.Begin(kid, GizmoMode.Rotate, GizmoSpace.World, GizmoAxis.Z, snap: false);
        drag6.Preview(0.6f);
        EditPlan sheared = drag6.Release();
        Check(sheared.Refusal == GizmoDragController.NotRepresentable && sheared.Commands.Count == 0 && Near16(pv6.Last(kid), kidStart, 0) && d6.Submissions == 0,
              "gizmo drag (J4): a rotate that would shear is refused at the release, nothing submitted, the preview reverted");
        // Translating the same child under the same parent is fine: the linear part is untouched.
        drag6.Begin(kid, GizmoMode.Translate, GizmoSpace.World, GizmoAxis.X, snap: false);
        drag6.Preview(1.0f);
        EditPlan slide = drag6.Release();
        Check(slide.Refusal is null && Submit(d6, tree6, new Selection(), slide) is null &&
              Near16(TransformMath.WorldOf(d6, tree6, kid), WorldMatrices.Multiply(Translate(1.0f, 0, 0), kidStart), 1e-4f),
              "gizmo drag (J4): a translate under the same parent commits and moves it exactly");

        // A sheared world matrix: only a world-axis translate, which needs no decomposition.
        var d7 = new FakeDocument();
        EntityGuid sq7 = d7.Add(1, null, Local(Vector3.Zero, new(0.3f, 0.5f, 0.2f), new(1, 1, 0.25f)));
        EntityGuid tilted = d7.Add(2, sq7, Local(new(1, 0, 0), new(0.4f, 0.1f, 0.7f), new(1, 1.5f, 1)));
        var tree7 = new DocumentTree();
        tree7.Rebuild(d7);
        var drag7 = new GizmoDragController(d7, tree7, new LocalTransforms(), new RecordingPreview());
        GizmoAvailability sheared7 = drag7.Availability(tilted, GizmoSpace.World);
        Check(sheared7.Translate && !sheared7.Rotate && !sheared7.Scale && !drag7.Begin(tilted, GizmoMode.Rotate, GizmoSpace.World, GizmoAxis.X, snap: false) &&
              !drag7.Begin(tilted, GizmoMode.Translate, GizmoSpace.Local, GizmoAxis.X, snap: false),
              "gizmo drag: a sheared world matrix allows only the world-axis translate");
        float[] shearStart = TransformMath.WorldOf(d7, tree7, tilted);
        drag7.Begin(tilted, GizmoMode.Translate, GizmoSpace.World, GizmoAxis.Z, snap: false);
        drag7.Preview(0.5f);
        EditPlan shearMove = drag7.Release();
        Check(shearMove.Refusal is null && Submit(d7, tree7, new Selection(), shearMove) is null &&
              Near16(TransformMath.WorldOf(d7, tree7, tilted), WorldMatrices.Multiply(Translate(0, 0, 0.5f), shearStart), 1e-4f),
              "gizmo drag: that translate commits and moves the node exactly");
    }

    private static float[] Translate(float x, float y, float z)
    {
        float[] m = WorldMatrices.Identity();
        m[12] = x;
        m[13] = y;
        m[14] = z;
        return m;
    }
}
