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
using System.Globalization;
using System.Linq;
using System.Numerics;
using System.Text;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Authoring;

namespace Atlantis.Editor.Model;

/// <summary>A planned edit: the transaction to submit, or the reason it is refused (nothing to submit).</summary>
public sealed record EditPlan(string Label, IReadOnlyList<Command> Commands, string? Refusal = null)
{
    public static EditPlan Refused(string label, string reason) => new(label, Array.Empty<Command>(), reason);
}

/// <summary>The intended, replayable state of one duplicate transaction.</summary>
public sealed record SubtreeDuplicate(EntityGuid Root, EditTransaction Edit)
{
    public bool Matches(IReadOnlyList<NodeState> observed)
    {
        var intended = Edit.Structure.OfType<NodeLife>().Select(entry => entry.State).ToList();
        return intended.Count == observed.Count && intended.Zip(observed).All(pair =>
            pair.First.Node == pair.Second.Node && pair.First.Position == pair.Second.Position &&
            pair.First.SameAs(pair.Second) && pair.First.Components.Zip(pair.Second.Components).All(component =>
                component.First.Values.Zip(component.Second.Values).All(value =>
                    ExactPayload(value.First.Value, value.Second.Value))));
    }

    private static bool ExactPayload(PropertyValue first, PropertyValue second)
    {
        static bool Same(float a, float b) => BitConverter.SingleToInt32Bits(a) == BitConverter.SingleToInt32Bits(b);
        if (first.TryGetFloat32(out float f) && second.TryGetFloat32(out float g)) return Same(f, g);
        if (first.TryGetVec3(out Vector3 a) && second.TryGetVec3(out Vector3 b))
            return Same(a.X, b.X) && Same(a.Y, b.Y) && Same(a.Z, b.Z);
        if (first.TryGetVec4(out Vector4 c) && second.TryGetVec4(out Vector4 d))
            return Same(c.X, d.X) && Same(c.Y, d.Y) && Same(c.Z, d.Z) && Same(c.W, d.W);
        return first.Equals(second);
    }
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

    private static IEnumerable<SceneAddress?> LocalAddresses(IReadOnlyList<SchemaType> schema, EntityGuid node)
    {
        yield return SceneSchema.AddressOf(schema, node, "Transform", "localPosition");
        yield return SceneSchema.AddressOf(schema, node, "Transform", "localEulerAnglesRadians");
        yield return SceneSchema.AddressOf(schema, node, "Transform", "localScale");
    }

    /// <summary>The three SetProperty commands writing a node's local Transform.</summary>
    public static IReadOnlyList<Command> WriteLocal(IReadOnlyList<SchemaType> schema, EntityGuid node, LocalTransform t)
    {
        Vector3[] values = [t.Position, t.EulerRadians, t.Scale];
        return LocalAddresses(schema, node).Zip(values)
            .Where(write => write.First.HasValue)
            .Select(write => (Command)new SetProperty(write.First!.Value, PropertyValue.FromVec3(write.Second))).ToList();
    }

    internal static IReadOnlyDictionary<EntityGuid, LocalTransform>? ReadCommittedLocals(
        IAuthoringDocument document, IReadOnlyList<EntityGuid> nodes)
    {
        var addresses = new List<SceneAddress>();
        foreach (EntityGuid node in nodes)
        {
            foreach (SceneAddress? address in LocalAddresses(document.Schema, node))
            {
                if (address is not SceneAddress field) return null;
                addresses.Add(field);
            }
        }
        var properties = document.GetProperties(addresses);
        if (properties.Count != addresses.Count) return null;
        var values = new List<Vector3>(properties.Count);
        foreach (PropertyValue? property in properties)
        {
            if (property is not PropertyValue value || !value.TryGetVec3(out Vector3 vector) ||
                !float.IsFinite(vector.X) || !float.IsFinite(vector.Y) || !float.IsFinite(vector.Z))
                return null;
            values.Add(vector);
        }
        var locals = new Dictionary<EntityGuid, LocalTransform>(nodes.Count);
        for (int i = 0; i < nodes.Count; ++i)
            locals.Add(nodes[i], new LocalTransform(values[3 * i], values[3 * i + 1], values[3 * i + 2]));
        return locals;
    }
}

public static class HierarchyCommands
{
    public const int MaxDuplicateNodes = 64;
    public const string AddMeshLabel = "add mesh to scene";

    public static EditPlan AddMesh(IReadOnlyList<SchemaType> schema, ulong position, EntityGuid node, AssetGuid mesh,
        Vector3? placement = null)
    {
        var renderable = schema.FirstOrDefault(type => type.Kind == TypeKind.Struct && type.ShortName == "Renderable");
        var field = renderable?.Fields.FirstOrDefault(field => field.Name == "meshAsset");
        if (renderable is null || field is not { Kind: TypeKind.Primitive, Primitive: PrimitiveKind.AssetGuid } ||
            (field.Flags & FieldFlags.Serializable) == 0)
            return EditPlan.Refused(AddMeshLabel, "the mesh field is unavailable");
        var address = new SceneAddress(node, renderable.Id, field.Id);
        Command[] commands = {
            new CreateNode(node, position, new[] { address.Component }),
            new SetProperty(address, PropertyValue.FromAsset(mesh))
        };
        if (placement is not Vector3 point) return new EditPlan(AddMeshLabel, commands);
        if (!ViewportMeshPlacement.IsFinite(point) ||
            SceneSchema.AddressOf(schema, node, "Transform", "localPosition") is not SceneAddress translation)
            return EditPlan.Refused(AddMeshLabel, "the placement translation is invalid or unavailable");
        return new EditPlan(AddMeshLabel, [.. commands, new SetProperty(translation, PropertyValue.FromVec3(point))]);
    }

    public static EditTransaction? AddedMeshHistory(IReadOnlyList<SchemaType> schema, NodeState? state, AssetGuid mesh,
        Vector3? placement = null)
    {
        if (state is null || state.Name.Length != 0 || state.Parent is not null || state.Children.Count != 0 ||
            state.Components.Count != 2) return null;
        var expected = new[] {
            ("Transform", "localPosition", PropertyValue.FromVec3(placement ?? Vector3.Zero)),
            ("Transform", "localEulerAnglesRadians", PropertyValue.FromVec3(Vector3.Zero)),
            ("Transform", "localScale", PropertyValue.FromVec3(Vector3.One)),
            ("Renderable", "meshAsset", PropertyValue.FromAsset(mesh)),
            ("Renderable", "materialAsset", PropertyValue.Absent)
        };
        var values = state.Components.SelectMany(component => component.Values).ToList();
        if (values.Count != expected.Length || expected.Any(field =>
            SceneSchema.AddressOf(schema, state.Node, field.Item1, field.Item2) is not SceneAddress address ||
            !values.Any(value => value.Address == address && value.Value.Equals(field.Item3)))) return null;
        return new EditTransaction(AddMeshLabel, Array.Empty<PropertyWrite>(), new EditEntry[] { new NodeLife(state, true) });
    }
    /// <summary>A rename against the latest observed name; labels never identify the node.</summary>
    public static EditPlan Rename(DocumentTree tree, EntityGuid node, string before, string name)
    {
        const string label = "rename node";
        if (!tree.Contains(node)) return EditPlan.Refused(label, "the node is gone");
        if (tree.NameOf(node) != before) return EditPlan.Refused(label, "changed elsewhere");
        if (!NodeNames.IsValid(name)) return EditPlan.Refused(label, "InvalidNodeName");
        if (before == name) return EditPlan.Refused(label, "unchanged");
        return new EditPlan(label, new Command[] { new SetNodeName(node, name) });
    }

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

    /// <summary>Builds the duplicate record from the current tree and complete authoring values.</summary>
    public static (SubtreeDuplicate? Copy, string? Refusal) DuplicateSubtree(
        IAuthoringDocument document, DocumentTree tree, EntityGuid source)
    {
        if (!tree.Contains(source)) return (null, "the selected node is gone");
        var nodes = new List<EntityGuid> { source };
        var seen = new HashSet<EntityGuid> { source };
        for (int i = 0; i < nodes.Count; ++i)
        {
            foreach (EntityGuid child in tree.ChildrenOf(nodes[i]))
            {
                if (!seen.Add(child)) return (null, "invalid source hierarchy");
                if (nodes.Count == MaxDuplicateNodes) return (null, $"the subtree exceeds {MaxDuplicateNodes} nodes");
                nodes.Add(child);
            }
        }

        var (entries, roots, refusal) = CopyNodes(document, tree, nodes, new[] { source });
        return entries is null ? (null, refusal) : (new SubtreeDuplicate(roots[0],
            new EditTransaction("duplicate subtree", Array.Empty<PropertyWrite>(), entries)), null);
    }

    public static (GuardedDuplicateEdit? Edit, string? Refusal) DuplicateSubtrees(
        IAuthoringDocument document, DocumentTree tree, IReadOnlyList<EntityGuid> selection)
    {
        if (!tree.OrderSynchronized || selection.Count < 2 || selection.Count > RequireDuplicateBatchRevision.MaxNodes)
            return (null, "select two or more live Hierarchy nodes");
        var selected = selection.ToHashSet();
        if (selected.Count != selection.Count || selected.Any(node => !tree.Contains(node)))
            return (null, "a selected node is no longer present");
        var roots = selected.Where(node => !selected.Any(ancestor =>
            ancestor != node && tree.IsSelfOrAncestor(ancestor, node))).ToList();
        var seen = new HashSet<EntityGuid>(roots);
        if (roots.Count == 0) return (null, "invalid source hierarchy");
        var nodes = new List<EntityGuid>(roots);
        for (int i = 0; i < nodes.Count; ++i)
        {
            foreach (EntityGuid child in tree.ChildrenOf(nodes[i]))
            {
                if (!seen.Add(child)) return (null, "invalid source hierarchy");
                if (nodes.Count == RequireDuplicateBatchRevision.MaxNodes)
                    return (null, $"the selected subtrees exceed {RequireDuplicateBatchRevision.MaxNodes} nodes in total");
                nodes.Add(child);
            }
        }
        var (entries, copiedRoots, refusal) = CopyNodes(document, tree, nodes, roots);
        if (entries is null) return (null, refusal);
        var dependencies = roots.Select(tree.ParentOf).OfType<EntityGuid>().ToHashSet();
        EntityGuid? predecessor = tree.Count == 0 ? null : tree.Order[^1];
        if (predecessor is EntityGuid precedingNode) dependencies.Add(precedingNode);
        return (new GuardedDuplicateEdit(copiedRoots, entries, tree.Count, predecessor,
            dependencies.ToDictionary(node => node, node => tree.IncarnationOf(node)!)), null);
    }

    private static (IReadOnlyList<NodeLife>? Entries, IReadOnlyList<EntityGuid> Roots, string? Refusal) CopyNodes(
        IAuthoringDocument document, DocumentTree tree, IReadOnlyList<EntityGuid> nodes, IReadOnlyList<EntityGuid> roots)
    {
        if (new DocumentView(document, tree).ReadCompleteNodes(nodes) is not { } snapshot)
            return (null, Array.Empty<EntityGuid>(), "the subtree's complete component values could not be read");
        var originals = snapshot.OrderBy(node => node.Position).ToArray();
        var copies = new Dictionary<EntityGuid, EntityGuid>(nodes.Count);
        var minted = new HashSet<EntityGuid>();
        foreach (EntityGuid original in nodes)
        {
            var copy = new EntityGuid(Guid.NewGuid());
            if (copy.Value == Guid.Empty || !minted.Add(copy) || tree.Contains(copy))
                return (null, Array.Empty<EntityGuid>(), "a copied node identity conflicts with the document");
            copies.Add(original, copy);
        }

        var rootSet = roots.ToHashSet();
        var rootNames = new Dictionary<EntityGuid, string>();
        foreach (var siblings in originals.Where(node => rootSet.Contains(node.Node)).GroupBy(node => node.Parent))
        {
            var names = tree.ChildrenOf(siblings.Key).Select(tree.NameOf).ToHashSet(StringComparer.Ordinal);
            foreach (NodeState root in siblings)
            {
                string name = DuplicateRootName(root.Name, names);
                rootNames.Add(root.Node, name);
                names.Add(name);
            }
        }

        var entries = new List<NodeLife>(nodes.Count);
        ulong position = (ulong)tree.Count;
        foreach (NodeState original in originals)
        {
            EntityGuid copy = copies[original.Node];
            bool isRoot = rootNames.TryGetValue(original.Node, out string? name);
            var components = original.Components.Select(component => new ComponentState(component.Type,
                component.Values.Select(value => (value.Address with { Node = copy }, value.Value)).ToList())).ToList();
            entries.Add(new NodeLife(new NodeState(copy, position++,
                isRoot ? original.Parent : copies[original.Parent!.Value],
                original.Children.Select(child => copies[child]).ToList(), components,
                isRoot ? name! : original.Name), true));
        }
        return (entries, originals.Where(node => rootSet.Contains(node.Node)).Select(node => copies[node.Node]).ToArray(), null);
    }

    private static string DuplicateRootName(string source, HashSet<string> names)
    {
        for (int number = 1; number <= names.Count + 1; ++number)
        {
            string suffix = number == 1 ? "Copy" : "Copy " + number.ToString(CultureInfo.InvariantCulture);
            if (source.Length > 0) suffix = " " + suffix;
            int remaining = NodeNames.MaxUtf8Bytes - suffix.Length;
            int length = 0;
            foreach (Rune scalar in source.EnumerateRunes())
            {
                remaining -= scalar.Utf8SequenceLength;
                if (remaining < 0) break;
                length += scalar.Utf16SequenceLength;
            }
            string candidate = source[..length] + suffix;
            if (!names.Contains(candidate)) return candidate;
        }
        throw new InvalidOperationException("no duplicate label in a finite set of siblings");
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

    public const string ReparentLabel = "reparent";

    public static EditPlan Reparent(IAuthoringDocument document, DocumentTree tree, EntityGuid node, EntityGuid? parent)
    {
        var (edit, refusal) = PlanReparent(document, tree, node, tree.ParentOf(node), parent);
        return edit is null ? EditPlan.Refused(ReparentLabel, refusal!)
            : new EditPlan(ReparentLabel, StructuralSteps.Commands(edit, forward: true, document.Schema));
    }

    /// <summary>Capture both the edit and its inverse from the same committed transform reads.</summary>
    public static (EditTransaction? Edit, string? Refusal) PlanReparent(IAuthoringDocument document, DocumentTree tree,
        EntityGuid node, EntityGuid? expectedParent, EntityGuid? parent)
    {
        if (!tree.Contains(node) || (parent is EntityGuid target && !tree.Contains(target)))
            return (null, "the source or destination is gone");
        if (tree.ParentOf(node) != expectedParent) return (null, "the source parent changed");
        if (expectedParent == parent) return (null, "it is already there");
        if (parent is EntityGuid p && tree.IsSelfOrAncestor(node, p)) return (null, "a node cannot be its own ancestor");

        List<EntityGuid>? Chain(EntityGuid? start)
        {
            var chain = new List<EntityGuid>();
            var seen = new HashSet<EntityGuid>();
            for (EntityGuid? current = start; current is EntityGuid id; current = tree.ParentOf(id))
            {
                if (!tree.Contains(id) || !seen.Add(id)) return null;
                chain.Add(id);
            }
            return chain;
        }

        var sourceChain = Chain(node);
        var parentChain = Chain(parent);
        if (sourceChain is null || parentChain is null) return (null, "the source or destination hierarchy is incomplete");
        var locals = TransformMath.ReadCommittedLocals(document, sourceChain.Union(parentChain).ToList());
        if (locals is null) return (null, "the source or destination has an unreadable Transform");

        float[] ComposeWorld(List<EntityGuid> chain)
        {
            float[] world = WorldMatrices.Identity();
            for (int i = chain.Count - 1; i >= 0; --i)
                world = WorldMatrices.Multiply(world, WorldMatrices.ComposeLocal(locals[chain[i]]));
            return world;
        }

        float[] world = ComposeWorld(sourceChain);
        float[] parentWorld = ComposeWorld(parentChain);
        if (world.Any(value => !float.IsFinite(value)) || parentWorld.Any(value => !float.IsFinite(value)))
            return (null, NotRepresentable);
        if (TransformMath.InverseAffine(parentWorld) is not float[] inverse)
            return (null, "the new parent's world matrix is not invertible");
        float[] localMatrix = WorldMatrices.Multiply(inverse, world);
        if (localMatrix.Any(value => !float.IsFinite(value)) || TransformMath.ToLocalTransform(localMatrix) is not LocalTransform local)
            return (null, NotRepresentable);
        float[] recomposed = WorldMatrices.Multiply(parentWorld, WorldMatrices.ComposeLocal(local));
        if (!recomposed.Zip(world).All(entry => MathF.Abs(entry.First - entry.Second) < 1e-4f))
            return (null, NotRepresentable);

        var before = TransformMath.WriteLocal(document.Schema, node, locals[node]).Cast<SetProperty>();
        var after = TransformMath.WriteLocal(document.Schema, node, local).Cast<SetProperty>();
        var writes = before.Zip(after).Select(pair => new PropertyWrite(pair.First.Address, pair.First.Value, pair.Second.Value)).ToList();
        return (new EditTransaction(ReparentLabel, writes, [new ParentEntry(node, expectedParent, parent)]), null);
    }
}
