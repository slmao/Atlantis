// Plan 0068 M2 (Spec 0068 R3, R5, Q5; Plan J4 as ruled; ADR-0135 D2): the
// Hierarchy's edits as authoring transactions from the closed command set --
// create a node under a parent (appended to the node order), destroy a node
// with its whole subtree (validation refuses an orphaned child, ParentExists;
// a destroyed active camera is cleared in the same transaction,
// ActiveCameraExists), and reparent keeping the node's world placement: its
// new local Transform is inverse(new parent's world) * its world, decomposed
// as a TRS within gizmo.h's tolerance -- where that is not possible (under a
// non-uniformly scaled parent) the reparent is refused with its reason and
// nothing is submitted (J4). World matrices are solved from the document's
// local Transforms as the authoring World solves them (WorldMatrices).
// UI-free.
using System;
using System.Collections.Generic;
using System.Numerics;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Authoring;

namespace Atlantis.Editor.Model;

/// <summary>A planned edit: the transaction to submit, or the reason it is refused (nothing to submit).</summary>
public sealed record EditPlan(string Label, IReadOnlyList<Command> Commands, string? Refusal = null)
{
    public static EditPlan Refused(string label, string reason) => new(label, Array.Empty<Command>(), reason);
}

public static class TransformMath
{
    /// <summary>A node's local Transform as the document holds it; identity when it has none.</summary>
    public static LocalTransform LocalOf(IAuthoringDocument document, EntityGuid node)
    {
        Vector3 Read(string field, Vector3 fallback)
        {
            SceneAddress? address = SceneSchema.AddressOf(document.Schema, node, "Transform", field);
            return address is SceneAddress a && document.GetProperty(a) is PropertyValue v && v.TryGetVec3(out Vector3 x) ? x : fallback;
        }
        return new LocalTransform(Read("localPosition", Vector3.Zero), Read("localEulerAnglesRadians", Vector3.Zero),
                                  Read("localScale", Vector3.One));
    }

    /// <summary>A node's world matrix: its local Transform under its parents', as the authoring World solves it.</summary>
    public static float[] WorldOf(IAuthoringDocument document, DocumentTree tree, EntityGuid node)
    {
        var chain = new List<EntityGuid>();
        var seen = new HashSet<EntityGuid>();
        for (EntityGuid? current = node; current is EntityGuid c && seen.Add(c); current = tree.ParentOf(c)) chain.Add(c);
        float[]? world = null;
        for (int i = chain.Count - 1; i >= 0; --i)
        {
            float[] local = WorldMatrices.ComposeLocal(LocalOf(document, chain[i]));
            world = world is null ? local : WorldMatrices.Multiply(world, local);
        }
        return world ?? WorldMatrices.Identity();
    }

    /// <summary>The inverse of an affine matrix (bottom row 0 0 0 1); null when its 3x3 is (near-)singular.</summary>
    public static float[]? InverseAffine(float[] m)
    {
        float a = m[0], b = m[4], c = m[8], d = m[1], e = m[5], f = m[9], g = m[2], h = m[6], k = m[10];
        float c00 = e * k - f * h, c01 = -(d * k - f * g), c02 = d * h - e * g;
        float det = a * c00 + b * c01 + c * c02;
        float scale = MathF.Abs(a) + MathF.Abs(b) + MathF.Abs(c) + MathF.Abs(d) + MathF.Abs(e) + MathF.Abs(f) + MathF.Abs(g) +
                      MathF.Abs(h) + MathF.Abs(k);
        if (!float.IsFinite(det) || MathF.Abs(det) <= 1e-12f * MathF.Max(1.0f, scale * scale * scale)) return null;
        float inv = 1.0f / det;
        // Row-major inverse rows (r0, r1, r2) from the adjugate, stored column-major.
        float i00 = c00 * inv, i01 = -(b * k - c * h) * inv, i02 = (b * f - c * e) * inv;
        float i10 = c01 * inv, i11 = (a * k - c * g) * inv, i12 = -(a * f - c * d) * inv;
        float i20 = c02 * inv, i21 = -(a * h - b * g) * inv, i22 = (a * e - b * d) * inv;
        float tx = m[12], ty = m[13], tz = m[14];
        return new[]
        {
            i00, i10, i20, 0.0f,
            i01, i11, i21, 0.0f,
            i02, i12, i22, 0.0f,
            -(i00 * tx + i01 * ty + i02 * tz), -(i10 * tx + i11 * ty + i12 * tz), -(i20 * tx + i21 * ty + i22 * tz), 1.0f,
        };
    }

    /// <summary>
    /// A matrix as a local Transform (position, Euler pitch/yaw/roll for R = Ry * Rx * Rz, scale); null when it is not
    /// a TRS within gizmo.h's shear tolerance. A reflection keeps its negative x scale.
    /// </summary>
    public static LocalTransform? ToLocalTransform(float[] m)
    {
        if (Gizmo.Decompose(m) is not Trs trs) return null;
        float R(int row, int column) => trs.Rotation[column * 3 + row];
        float sinPitch = Math.Clamp(-R(1, 2), -1.0f, 1.0f);
        float pitch = MathF.Asin(sinPitch);
        float yaw, roll;
        if (MathF.Abs(sinPitch) < 0.9999999f)
        {
            yaw = MathF.Atan2(R(0, 2), R(2, 2));
            roll = MathF.Atan2(R(1, 0), R(1, 1));
        }
        else
        {
            yaw = MathF.Atan2(-R(2, 0), R(0, 0));  // gimbal lock: the roll folds into the yaw
            roll = 0.0f;
        }
        return new LocalTransform(new Vector3(trs.Translation[0], trs.Translation[1], trs.Translation[2]), new Vector3(pitch, yaw, roll),
                                  new Vector3(trs.Scale[0], trs.Scale[1], trs.Scale[2]));
    }

    /// <summary>The three SetProperty commands writing a node's local Transform.</summary>
    public static IReadOnlyList<Command> WriteLocal(IReadOnlyList<SchemaType> schema, EntityGuid node, LocalTransform t)
    {
        var commands = new List<Command>();
        foreach (var (field, value) in new[] { ("localPosition", t.Position), ("localEulerAnglesRadians", t.EulerRadians), ("localScale", t.Scale) })
        {
            if (SceneSchema.AddressOf(schema, node, "Transform", field) is SceneAddress address)
            {
                commands.Add(new SetProperty(address, PropertyValue.FromVec3(value)));
            }
        }
        return commands;
    }
}

public static class HierarchyCommands
{
    public const string NotRepresentable =
        "its world placement cannot be kept under that parent: the local transform would not be a translation, rotation " +
        "and scale (a non-uniformly scaled parent)";

    /// <summary>A node (it has its Transform), under <paramref name="parent"/> (or at the root), appended to the node order.</summary>
    public static (EditPlan Plan, EntityGuid Node) CreateChild(IAuthoringDocument document, DocumentTree tree, EntityGuid? parent)
    {
        var node = new EntityGuid(Guid.NewGuid());
        // A node is created with its Transform already (it is a required component): the list names only extra ones.
        var commands = new List<Command> { new CreateNode(node, (ulong)tree.Count, Array.Empty<TypeId>()) };
        if (parent is EntityGuid p) commands.Add(new SetParent(node, p));
        return (new EditPlan("create node", commands), node);
    }

    /// <summary>The node and its whole subtree, in one transaction; the active camera cleared first if it is among them.</summary>
    public static EditPlan DestroySubtree(IAuthoringDocument document, DocumentTree tree, EntityGuid node)
    {
        var nodes = new List<EntityGuid> { node };
        nodes.AddRange(tree.DescendantsOf(node));
        var commands = new List<Command>();
        if (document.ActiveCamera() is EntityGuid camera && nodes.Contains(camera)) commands.Add(new SetActiveCamera(null));
        foreach (EntityGuid n in nodes) commands.Add(new DestroyNode(n));
        return new EditPlan(nodes.Count == 1 ? "destroy node" : $"destroy {nodes.Count} nodes", commands);
    }

    /// <summary>
    /// The node under <paramref name="parent"/> (null: the root), keeping its world placement; refused when the node
    /// would become its own ancestor, or when the placement cannot be kept (J4).
    /// </summary>
    public static EditPlan Reparent(IAuthoringDocument document, DocumentTree tree, EntityGuid node, EntityGuid? parent)
    {
        const string label = "reparent";
        if (tree.ParentOf(node) == parent) return EditPlan.Refused(label, "it is already there");
        if (parent is EntityGuid p && tree.IsSelfOrAncestor(node, p)) return EditPlan.Refused(label, "a node cannot be its own ancestor");
        float[] world = TransformMath.WorldOf(document, tree, node);
        float[] parentWorld = parent is EntityGuid q ? TransformMath.WorldOf(document, tree, q) : WorldMatrices.Identity();
        if (TransformMath.InverseAffine(parentWorld) is not float[] inverse) return EditPlan.Refused(label, "the new parent's world matrix is not invertible");
        if (TransformMath.ToLocalTransform(WorldMatrices.Multiply(inverse, world)) is not LocalTransform local)
        {
            return EditPlan.Refused(label, NotRepresentable);
        }
        var commands = new List<Command> { new SetParent(node, parent) };
        commands.AddRange(TransformMath.WriteLocal(document.Schema, node, local));
        return new EditPlan(label, commands);
    }
}
