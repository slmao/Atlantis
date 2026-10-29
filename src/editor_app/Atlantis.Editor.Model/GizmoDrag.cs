// Plan 0068 M4 (Spec 0068 R5, R7, Q1; Plan J3 as ruled; ADR-0135 D1, D2): a
// Gizmo drag on the two documents. A drag previews on the Runtime World, which
// has no hierarchy: each pointer batch writes the new WorldMatrix of the
// dragged node and of each of its descendants (the document's hierarchy gives
// them) -- every descendant keeps its placement relative to the node -- up to
// 256 descendants, beyond which only the node previews (J3). Previews are never
// recorded and never written to the document. The release converts the final
// world matrix to the node's local Transform through its parent's world matrix
// and plans ONE authoring transaction -- the mode's field only (position for
// Translate, Euler angles for Rotate, scale for Scale) -- or refuses where the
// result would not be a translation, rotation and scale under that parent (the
// rule of J4); a refused release reverts the preview. A new C# model with no
// C++ reference (ADR-0137 D2). UI-free.
using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using Atlantis.Gameplay;

namespace Atlantis.Editor.Model;

/// <summary>Where previews go: the Runtime World's WorldMatrix (RuntimeConnection), one transaction per batch.</summary>
public interface IWorldPreview
{
    void Write(IReadOnlyList<(EntityGuid Node, float[] World)> matrices);
}

/// <summary>What a Gizmo can do for a node: Translate always; Rotate and Scale where their result stays a TRS under the parent.</summary>
public sealed record GizmoAvailability(bool Translate, bool Rotate, bool Scale, string? Reason)
{
    public bool Allows(GizmoMode mode) => mode switch { GizmoMode.Translate => Translate, GizmoMode.Rotate => Rotate, _ => Scale };
}

/// <summary>What the Gizmo shows for a world matrix: the effective mode and space, and the axes' world directions.</summary>
public static class GizmoLayout
{
    /// <summary>The requested mode where the node allows it, else Translate.</summary>
    public static GizmoMode EffectiveMode(GizmoAvailability availability, GizmoMode requested) =>
        availability.Allows(requested) ? requested : GizmoMode.Translate;

    /// <summary>The requested space where the world matrix has a TRS form (the entity's own axes need it), else World.</summary>
    public static GizmoSpace EffectiveSpace(float[] world, GizmoSpace requested) => Gizmo.Decompose(world) is null ? GizmoSpace.World : requested;

    /// <summary>The world-space unit direction of each axis for the mode and space (the world's own when there is no TRS form).</summary>
    public static Vector3[] Directions(float[] world, GizmoMode mode, GizmoSpace space)
    {
        Trs trs = Gizmo.Decompose(world) ?? Trs.Identity();
        return new[]
        {
            Gizmo.AxisDirection(trs, mode, space, GizmoAxis.X), Gizmo.AxisDirection(trs, mode, space, GizmoAxis.Y), Gizmo.AxisDirection(trs, mode, space, GizmoAxis.Z),
        };
    }
}

public sealed class GizmoDragController
{
    /// <summary>J3 (ruled): a drag previews the node and up to this many descendants; beyond it, only the node.</summary>
    public const int MaxPreviewDescendants = 256;

    public const string NotRepresentable =
        "the result would not be a translation, rotation and scale under its parent (a non-uniformly scaled parent)";

    private readonly IAuthoringDocument _document;
    private readonly DocumentTree _tree;
    private readonly LocalTransforms _transforms;
    private readonly IWorldPreview _preview;

    // The drag in progress.
    private EntityGuid? _node;
    private GizmoMode _mode;
    private GizmoSpace _space;
    private GizmoAxis _axis;
    private bool _snap;
    private float[] _startWorld = WorldMatrices.Identity();
    private float[]? _startInverse;
    private List<(EntityGuid Node, float[] World)> _descendants = new();
    private float[]? _latest;

    public GizmoDragController(IAuthoringDocument document, DocumentTree tree, LocalTransforms transforms, IWorldPreview preview)
    {
        _document = document;
        _tree = tree;
        _transforms = transforms;
        _preview = preview;
    }

    public bool Dragging => _node is not null;

    /// <summary>The axis of the drag in progress.</summary>
    public GizmoAxis? Axis => _node is null ? null : _axis;

    /// <summary>The number of descendants previewed with the node in the drag in progress (0 when capped or none).</summary>
    public int PreviewedDescendants => _descendants.Count;

    /// <summary>The previewed world matrix of the node (the drag's latest), or its start.</summary>
    public float[]? LatestWorld => _latest;

    /// <summary>Which modes a Gizmo can edit for <paramref name="node"/> in <paramref name="space"/>, by trying each on every axis.</summary>
    public GizmoAvailability Availability(EntityGuid node, GizmoSpace space)
    {
        float[] world = _transforms.WorldOf(_document, _tree, node);
        if (Gizmo.Decompose(world) is null) return new GizmoAvailability(true, false, false, "its world matrix has a shear: only a world-axis translate is possible");
        float[]? parentInverse = ParentInverse(node);
        if (parentInverse is null) return new GizmoAvailability(true, false, false, "its parent's world matrix is not invertible");
        bool Ok(GizmoMode mode, float amount)
        {
            for (int axis = 0; axis < 3; ++axis)
            {
                float[] moved = Gizmo.DragMatrix(world, mode, space, (GizmoAxis)axis, amount, snap: false);
                if (TransformMath.ToLocalTransform(WorldMatrices.Multiply(parentInverse, moved)) is null) return false;
            }
            return true;
        }
        bool rotate = Ok(GizmoMode.Rotate, 0.3f), scale = Ok(GizmoMode.Scale, 1.25f);
        string? reason = rotate && scale ? null : "a non-uniformly scaled parent: " + (rotate ? "scale" : scale ? "rotate" : "rotate and scale") + " would shear it";
        return new GizmoAvailability(true, rotate, scale, reason);
    }

    /// <summary>Starts a drag; false (nothing started) when the Gizmo cannot edit the node in this mode.</summary>
    public bool Begin(EntityGuid node, GizmoMode mode, GizmoSpace space, GizmoAxis axis, bool snap)
    {
        if (Dragging || !_tree.Contains(node)) return false;
        float[] world = _transforms.WorldOf(_document, _tree, node);
        bool translateWorld = mode == GizmoMode.Translate && space == GizmoSpace.World;
        if (Gizmo.Decompose(world) is null && !translateWorld) return false;
        _node = node;
        _mode = mode;
        _space = space;
        _axis = axis;
        _snap = snap;
        _startWorld = world;
        _startInverse = TransformMath.InverseAffine(world);
        _latest = world;
        _descendants = new List<(EntityGuid, float[])>();
        IReadOnlyList<EntityGuid> below = _tree.DescendantsOf(node);
        if (below.Count <= MaxPreviewDescendants && _startInverse is not null)
        {
            foreach (EntityGuid d in below) _descendants.Add((d, _transforms.WorldOf(_document, _tree, d)));
        }
        return true;
    }

    /// <summary>One pointer batch: the node's matrix for the drag amount (and its descendants'), written to the Runtime World.</summary>
    public float[]? Preview(float amount)
    {
        if (_node is not EntityGuid node || !float.IsFinite(amount)) return null;
        float[] world = NewWorld(amount);
        _latest = world;
        var writes = new List<(EntityGuid, float[])>(1 + _descendants.Count) { (node, world) };
        if (_descendants.Count > 0 && _startInverse is not null)
        {
            // Each descendant keeps its placement relative to the node: new = (world * inverse(start)) * its start.
            float[] delta = WorldMatrices.Multiply(world, _startInverse);
            foreach (var (d, startWorld) in _descendants) writes.Add((d, WorldMatrices.Multiply(delta, startWorld)));
        }
        _preview.Write(writes);
        return world;
    }

    /// <summary>
    /// The release: the one authoring transaction for the final matrix, or its refusal (nothing to submit; the preview is
    /// reverted). The drag ends either way. If the document then refuses the transaction, call <see cref="Revert"/>.
    /// </summary>
    public EditPlan Release()
    {
        if (_node is not EntityGuid node || _latest is null) return EditPlan.Refused("drag", "no drag");
        string label = _mode switch { GizmoMode.Translate => "move", GizmoMode.Rotate => "rotate", _ => "scale" };
        float[] final = _latest;
        EditPlan Refuse(string reason)
        {
            Revert();
            return EditPlan.Refused(label, reason);
        }
        if (final.SequenceEqual(_startWorld)) return Refuse("unchanged");
        float[]? parentInverse = ParentInverse(node);
        if (parentInverse is null) return Refuse("its parent's world matrix is not invertible");
        if (TransformMath.ToLocalTransform(WorldMatrices.Multiply(parentInverse, final)) is not LocalTransform local) return Refuse(NotRepresentable);
        LocalTransform current = _transforms.Get(_document, node);
        string field = _mode switch { GizmoMode.Translate => "localPosition", GizmoMode.Rotate => "localEulerAnglesRadians", _ => "localScale" };
        Vector3 value = _mode switch { GizmoMode.Translate => local.Position, GizmoMode.Rotate => local.EulerRadians, _ => local.Scale };
        Vector3 before = _mode switch { GizmoMode.Translate => current.Position, GizmoMode.Rotate => current.EulerRadians, _ => current.Scale };
        if (value == before) return Refuse("unchanged");
        if (SceneSchema.AddressOf(_document.Schema, node, "Transform", field) is not Atlantis.Gameplay.Authoring.SceneAddress address)
        {
            return Refuse("the schema has no Transform." + field);
        }
        var commands = new Command[] { new SetProperty(address, PropertyValue.FromVec3(value)) };
        _node = null;
        return new EditPlan(label, commands);
    }

    /// <summary>Writes the start matrices back (a refused release, or a cancelled drag) and ends the drag.</summary>
    public void Revert()
    {
        if (_node is EntityGuid node)
        {
            var writes = new List<(EntityGuid, float[])>(1 + _descendants.Count) { (node, _startWorld) };
            writes.AddRange(_descendants);
            _preview.Write(writes);
        }
        _node = null;
        _latest = null;
        _descendants = new List<(EntityGuid, float[])>();
    }

    /// <summary>Reverts a release the document refused: the preview of the last drag goes back to its start.</summary>
    public void RevertRelease(EntityGuid node)
    {
        if (!_tree.Contains(node)) return;
        var writes = new List<(EntityGuid, float[])> { (node, _startWorld) };
        writes.AddRange(_descendants);
        _preview.Write(writes);
    }

    private float[] NewWorld(float amount)
    {
        if (Gizmo.Decompose(_startWorld) is not null) return Gizmo.DragMatrix(_startWorld, _mode, _space, _axis, amount, _snap);
        // No TRS form (a sheared world): only a world-axis translate is offered, and it needs no decomposition.
        float[] moved = (float[])_startWorld.Clone();
        if (_snap) amount = Gizmo.SnapAmount(GizmoMode.Translate, amount);
        moved[12 + (int)_axis] = _startWorld[12 + (int)_axis] + amount;
        return moved;
    }

    private float[]? ParentInverse(EntityGuid node)
    {
        float[] parentWorld = _tree.ParentOf(node) is EntityGuid p ? _transforms.WorldOf(_document, _tree, p) : WorldMatrices.Identity();
        return TransformMath.InverseAffine(parentWorld);
    }
}
