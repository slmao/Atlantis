using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Authoring;

namespace Atlantis.Editor.Model;

public sealed record GroupGizmoSample(IReadOnlyList<PropertyWrite> Writes,
    IReadOnlyList<float[]> Worlds, string? Refusal);

/// <summary>The authoritative, immutable placement inputs for one displayed group Gizmo.</summary>
public sealed class GroupGizmo
{
    private sealed record Edge(EntityGuid Node, EntityGuid? Parent, object Identity, object Stamp);
    private sealed record Root(EntityGuid Node, LocalTransform Local, float[] World, float[]? ParentInverse,
        BatchPropertyTarget[] Fields);

    private readonly Edge[] _paths;
    private readonly Edge[] _placementPaths;
    private readonly Root[] _roots;
    public IReadOnlyList<BatchPropertyTarget> Targets { get; }
    public Vector3 Pivot { get; }
    public IEnumerable<EntityGuid> Roots => _roots.Select(root => root.Node);

    private GroupGizmo(Edge[] paths, Edge[] placementPaths, Root[] roots, BatchPropertyTarget[] targets)
    {
        _paths = paths;
        _placementPaths = placementPaths;
        _roots = roots;
        Targets = targets;
        Pivot = new Vector3((float)roots.Average(root => (double)root.World[12]),
            (float)roots.Average(root => (double)root.World[13]), (float)roots.Average(root => (double)root.World[14]));
    }

    public static GroupGizmo? Read(IAuthoringDocument document, DocumentTree tree, IReadOnlyList<EntityGuid> selection)
    {
        if (selection.Count is < 2 or > RequireTransformBatchRevision.MaxNodes || !tree.OrderSynchronized) return null;
        var selected = selection.ToHashSet();
        var roots = new List<EntityGuid>();
        var paths = new Dictionary<EntityGuid, Edge>();
        var placement = new HashSet<EntityGuid>();
        foreach (EntityGuid node in tree.Order.Where(selected.Contains))
        {
            var path = new HashSet<EntityGuid>();
            bool inherited = false;
            for (EntityGuid? next = node; next is EntityGuid member; next = tree.ParentOf(member))
            {
                if (!path.Add(member) || tree.IncarnationOf(member) is not object identity ||
                    tree.ParentStamp(member) is not object stamp) return null;
                paths.TryAdd(member, new Edge(member, tree.ParentOf(member), identity, stamp));
                if (member != node && selected.Contains(member)) inherited = true;
            }
            if (inherited) continue;
            roots.Add(node);
            placement.UnionWith(path);
        }
        if (roots.Count == 0 || !selected.All(paths.ContainsKey)) return null;

        string[] fields = ["localPosition", "localEulerAnglesRadians", "localScale"];
        EntityGuid[] needed = tree.Order.Where(placement.Contains).ToArray();
        var addresses = new List<SceneAddress>(needed.Length * fields.Length);
        foreach (EntityGuid node in needed)
        foreach (string field in fields)
        {
            if (SceneSchema.AddressOf(document.Schema, node, "Transform", field) is not SceneAddress address)
                return null;
            addresses.Add(address);
        }
        var values = document.GetProperties(addresses);
        if (values.Count != addresses.Count) return null;
        var targets = new BatchPropertyTarget[addresses.Count];
        var locals = new Dictionary<EntityGuid, LocalTransform>();
        var byNode = new Dictionary<EntityGuid, BatchPropertyTarget[]>();
        for (int i = 0; i < needed.Length; ++i)
        {
            var vectors = new Vector3[3];
            EntityGuid node = needed[i];
            for (int j = 0; j < fields.Length; ++j)
            {
                int index = i * fields.Length + j;
                SceneAddress address = addresses[index];
                if (values[index] is not PropertyValue value || !value.TryGetVec3(out vectors[j]) ||
                    !Finite(vectors[j]) || tree.ComponentIncarnationOf(address) is not object component ||
                    tree.PropertyStamp(address) is not object stamp) return null;
                targets[index] = new BatchPropertyTarget(address, value, paths[node].Identity, component, stamp);
            }
            locals.Add(node, new LocalTransform(vectors[0], vectors[1], vectors[2]));
            byNode.Add(node, targets[(i * fields.Length)..((i + 1) * fields.Length)]);
        }

        var worlds = new Dictionary<EntityGuid, float[]>();
        float[] WorldOf(EntityGuid? node)
        {
            var chain = new Stack<EntityGuid>();
            for (; node is EntityGuid id && !worlds.ContainsKey(id); node = paths[id].Parent) chain.Push(id);
            var world = node is EntityGuid parent ? worlds[parent] : WorldMatrices.Identity();
            while (chain.TryPop(out EntityGuid current))
            {
                world = WorldMatrices.Multiply(world, WorldMatrices.ComposeLocal(locals[current]));
                worlds.Add(current, world);
            }
            return world;
        }
        Root[] computed = roots.Select(node => new Root(node, locals[node], WorldOf(node),
            TransformMath.InverseAffine(WorldOf(paths[node].Parent)), byNode[node])).ToArray();
        if (computed.Any(root => !root.World.All(float.IsFinite))) return null;
        return new GroupGizmo(paths.Values.ToArray(), needed.Select(node => paths[node]).ToArray(), computed, targets);
    }

    public bool IsCurrent(DocumentTree tree, bool preview, bool checkStamps)
    {
        return (preview ? _paths : _placementPaths).All(edge =>
            ReferenceEquals(edge.Identity, tree.IncarnationOf(edge.Node)) &&
            edge.Parent == tree.ParentOf(edge.Node) &&
            (!checkStamps || ReferenceEquals(edge.Stamp, tree.ParentStamp(edge.Node))));
    }

    public bool SameLayout(GroupGizmo other) =>
        Pivot == other.Pivot && Roots.SequenceEqual(other.Roots) &&
        Targets.SequenceEqual(other.Targets) && _paths.SequenceEqual(other._paths);

    public GroupGizmoSample Solve(GizmoMode mode, GizmoAxis axis, float amount, bool snap)
    {
        if (!float.IsFinite(amount)) return new GroupGizmoSample([], [], "The drag amount is not finite.");
        if (snap) amount = Gizmo.SnapAmount(mode, amount);
        if (mode == GizmoMode.Rotate) amount = MathF.IEEERemainder(amount, 2 * MathF.PI);
        if (mode == GizmoMode.Scale) amount = MathF.Max(amount, 1e-4f);
        if (amount == (mode == GizmoMode.Scale ? 1 : 0))
            return new GroupGizmoSample([], _roots.Select(root => root.World).ToArray(), null);
        Vector3 direction = axis switch { GizmoAxis.X => Vector3.UnitX, GizmoAxis.Y => Vector3.UnitY, _ => Vector3.UnitZ };
        var delta = mode switch
        {
            GizmoMode.Translate => WorldMatrices.ComposeLocal(LocalTransform.Identity with { Position = amount * direction }),
            GizmoMode.Rotate => AroundPivot(LocalTransform.Identity with { EulerRadians = amount * direction }),
            _ => AroundPivot(LocalTransform.Identity with { Scale = new Vector3(amount) })
        };
        var writes = new List<PropertyWrite>();
        var worlds = new List<float[]>();
        foreach (Root root in _roots)
        {
            if (root.ParentInverse is not float[] inverse)
                return new GroupGizmoSample([], [], $"The parent of {root.Node} is not invertible.");
            var desired = WorldMatrices.Multiply(delta, root.World);
            var local = WorldMatrices.Multiply(inverse, desired);
            if (!desired.All(float.IsFinite) || !local.All(float.IsFinite) ||
                SolveLocal(root.Local, local, mode, amount) is not LocalTransform result)
                return new GroupGizmoSample([], [], $"The transform of {root.Node} cannot be represented as local TRS.");
            Vector3[] leaves = [result.Position, result.EulerRadians, result.Scale];
            for (int i = 0; i < leaves.Length; ++i)
            {
                BatchPropertyTarget field = root.Fields[i];
                PropertyValue before = field.Value!.Value;
                PropertyValue after = PropertyValue.FromVec3(leaves[i]);
                if (before != after) writes.Add(new PropertyWrite(field.Address, before, after));
            }
            worlds.Add(desired);
        }
        return new GroupGizmoSample(writes, worlds, null);
    }

    private float[] AroundPivot(LocalTransform linear) => WorldMatrices.Multiply(
        WorldMatrices.ComposeLocal(LocalTransform.Identity with { Position = Pivot }), WorldMatrices.Multiply(
            WorldMatrices.ComposeLocal(linear), WorldMatrices.ComposeLocal(LocalTransform.Identity with { Position = -Pivot })));

    private static LocalTransform? SolveLocal(LocalTransform before, float[] desired, GizmoMode mode, float factor)
    {
        if (TransformMath.ToLocalTransform(desired) is not LocalTransform solved) return null;
        LocalTransform result = before with { Position = solved.Position };
        if (mode == GizmoMode.Scale) result = result with { Scale = before.Scale * factor };
        if (mode == GizmoMode.Rotate && !MatchesBasis(desired, result))
        {
            result = solved with { Scale = before.Scale };
            if (!MatchesBasis(desired, result)) result = solved;
        }
        return MatchesBasis(desired, result) ? result : null;
    }

    private static bool MatchesBasis(float[] desired, LocalTransform value)
    {
        var actual = WorldMatrices.ComposeLocal(value);
        for (int i = 0; i < 12; i += 4)
        {
            var expected = new Vector3(desired[i], desired[i + 1], desired[i + 2]);
            var observed = new Vector3(actual[i], actual[i + 1], actual[i + 2]);
            if (!Finite(observed) ||
                Vector3.Distance(observed, expected) > Gizmo.ShearTolerance * expected.Length()) return false;
        }
        return true;
    }

    private static bool Finite(Vector3 v) => float.IsFinite(v.X) && float.IsFinite(v.Y) && float.IsFinite(v.Z);
}
