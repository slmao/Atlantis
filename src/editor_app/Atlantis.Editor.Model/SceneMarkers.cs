// Plan 0068 M2 (Spec 0068 R3, R6): what the Hierarchy and the Viewport show
// of the document beyond the tree itself -- every node's local Transform,
// read once and kept current by PropertyChanged events (a deep scene such as
// Bistro would otherwise read every ancestor's Transform for every world
// matrix); world matrices solved from it; the markers of lights and cameras
// without a Renderable (E1's markers, picked client-side, drawn as overlay
// points); and the Hierarchy's rows, flattened depth first for a view.
// UI-free.
using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Authoring;

namespace Atlantis.Editor.Model;

/// <summary>Every node's local Transform as the document holds it, read on first use and kept current by events.</summary>
public sealed class LocalTransforms
{
    private readonly Dictionary<EntityGuid, LocalTransform> _cache = new();

    public LocalTransform Get(IAuthoringDocument document, EntityGuid node)
    {
        if (!_cache.TryGetValue(node, out LocalTransform local))
        {
            local = TransformMath.LocalOf(document, node);
            _cache[node] = local;
        }
        return local;
    }

    /// <summary>Reads the nodes' local Transforms not yet cached in one pipelined batch.</summary>
    public void Prefetch(IAuthoringDocument document, IEnumerable<EntityGuid> nodes)
    {
        var missing = nodes.Where(n => !_cache.ContainsKey(n)).Distinct().ToList();
        var addresses = new List<SceneAddress>();
        var owners = new List<(EntityGuid Node, int Field)>();
        string[] fields = { "localPosition", "localEulerAnglesRadians", "localScale" };
        foreach (EntityGuid node in missing)
        {
            for (int f = 0; f < fields.Length; ++f)
            {
                if (SceneSchema.AddressOf(document.Schema, node, "Transform", fields[f]) is SceneAddress a)
                {
                    addresses.Add(a);
                    owners.Add((node, f));
                }
            }
        }
        IReadOnlyList<PropertyValue?> values = document.GetProperties(addresses);
        var read = missing.ToDictionary(n => n, _ => new Vector3?[3]);
        for (int i = 0; i < owners.Count; ++i)
        {
            if (values[i] is PropertyValue v && v.TryGetVec3(out Vector3 x)) read[owners[i].Node][owners[i].Field] = x;
        }
        foreach (EntityGuid node in missing)
        {
            Vector3?[] r = read[node];
            _cache[node] = new LocalTransform(r[0] ?? Vector3.Zero, r[1] ?? Vector3.Zero, r[2] ?? Vector3.One);
        }
    }

    /// <summary>A node's world matrix, solved from the cached local Transforms up its parent chain.</summary>
    public float[] WorldOf(IAuthoringDocument document, DocumentTree tree, EntityGuid node)
    {
        var chain = new List<EntityGuid>();
        var seen = new HashSet<EntityGuid>();
        for (EntityGuid? current = node; current is EntityGuid c && seen.Add(c); current = tree.ParentOf(c)) chain.Add(c);
        float[]? world = null;
        for (int i = chain.Count - 1; i >= 0; --i)
        {
            float[] local = WorldMatrices.ComposeLocal(Get(document, chain[i]));
            world = world is null ? local : WorldMatrices.Multiply(world, local);
        }
        return world ?? WorldMatrices.Identity();
    }

    /// <summary>Follows one authoring event: a changed, added or removed Transform, a destroyed node, a reopened document.</summary>
    public void Apply(Event e)
    {
        switch (e)
        {
            case PropertyChanged changed: _cache.Remove(changed.Address.Node); break;
            case ComponentAdded added: _cache.Remove(added.Node); break;
            case ComponentRemoved removed: _cache.Remove(removed.Node); break;
            case NodeDestroyed destroyed: _cache.Remove(destroyed.Node); break;
            case DocumentReopened: _cache.Clear(); break;
        }
    }
}

/// <summary>One Hierarchy row: the node, its depth under the roots, and its label.</summary>
public sealed record HierarchyRow(EntityGuid Node, EntityGuid? Parent, int Depth, string Label);

public static class SceneMarkers
{
    /// <summary>Lights and cameras without a Renderable, at their world translation (E1's markers).</summary>
    public static IReadOnlyList<Marker> Of(IAuthoringDocument document, DocumentTree tree, LocalTransforms transforms)
    {
        IReadOnlyList<SchemaType> schema = document.Schema;
        TypeId? light = SceneSchema.TypeOf(schema, "Light"), camera = SceneSchema.TypeOf(schema, "Camera"),
                renderable = SceneSchema.TypeOf(schema, "Renderable");
        var markers = new List<Marker>();
        var wanted = new List<(EntityGuid Node, MarkerKind Kind)>();
        foreach (EntityGuid node in tree.Order)
        {
            IReadOnlyList<TypeId> components = tree.ComponentsOf(node);
            if (renderable is TypeId r && components.Contains(r)) continue;
            MarkerKind? kind = light is TypeId l && components.Contains(l) ? MarkerKind.Light
                             : camera is TypeId c && components.Contains(c) ? MarkerKind.Camera : null;
            if (kind is MarkerKind k) wanted.Add((node, k));
        }
        // Every marker's parent chain in one pipelined read, not a frame per property.
        var chains = new List<EntityGuid>();
        foreach (var (node, _) in wanted)
        {
            var seen = new HashSet<EntityGuid>();
            for (EntityGuid? current = node; current is EntityGuid c && seen.Add(c); current = tree.ParentOf(c)) chains.Add(c);
        }
        transforms.Prefetch(document, chains);
        foreach (var (node, k) in wanted)
        {
            float[] world = transforms.WorldOf(document, tree, node);
            markers.Add(new Marker(node, k, new Vector3(world[12], world[13], world[14])));
        }
        return markers;
    }

    /// <summary>The tree flattened depth first (each node, then its children), with depths and labels.</summary>
    public static IReadOnlyList<HierarchyRow> Rows(DocumentTree tree, IReadOnlyList<SchemaType> schema)
    {
        var rows = new List<HierarchyRow>(tree.Count);
        var stack = new Stack<(EntityGuid Node, int Depth)>();
        foreach (EntityGuid root in tree.ChildrenOf(null).Reverse()) stack.Push((root, 0));
        while (stack.Count > 0)
        {
            var (node, depth) = stack.Pop();
            rows.Add(new HierarchyRow(node, tree.ParentOf(node), depth, tree.Label(node, schema)));
            foreach (EntityGuid child in tree.ChildrenOf(node).Reverse()) stack.Push((child, depth + 1));
        }
        return rows;
    }
}
