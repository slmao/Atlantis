using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Authoring;

namespace Atlantis.Editor.Model;

public sealed class BatchRenameIntent(long lifetime, ulong selectionGeneration, IReadOnlyList<EntityGuid> nodes)
{
    private volatile bool _current = true;
    public long Lifetime { get; } = lifetime;
    public ulong SelectionGeneration { get; } = selectionGeneration;
    public IReadOnlyList<EntityGuid> Nodes { get; } = nodes.ToArray();
    public bool Current => _current;
    public void Cancel() => _current = false;
}

public sealed record BatchRenameTarget(EntityGuid Node, EntityGuid? Parent, string Before, int Position,
    string ParentPath, object Occurrence);
public sealed record BatchRenameRow(BatchRenameTarget Target, string After)
{
    public bool Changed => Target.Before != After;
    public override string ToString() =>
        $"{(Target.Before.Length == 0 ? "(unnamed)" : Target.Before)}  →  {After}{(Changed ? "" : "  (unchanged)")}\n" +
        $"{Target.ParentPath}  ·  {Target.Node}";
}

public sealed record BatchRenamePreview(DocumentToken Document, HierarchyFilter Filter, IReadOnlyList<BatchRenameTarget> Targets)
{
    public IReadOnlyList<BatchRenameRow>? Propose(string? prefix)
    {
        if (string.IsNullOrWhiteSpace(prefix)) return null;
        var rows = Targets.Select((target, index) => new BatchRenameRow(target,
            prefix + " " + (index + 1).ToString("D2", CultureInfo.InvariantCulture))).ToArray();
        return rows.All(row => NodeNames.IsValid(row.After)) ? rows : null;
    }

    public static BatchRenamePreview? Capture(DocumentToken document, HierarchyFilter filter, DocumentTree tree,
        IReadOnlyList<NodeSummary> roster, IReadOnlyList<EntityGuid> selected)
    {
        if (selected.Count < 2 || selected.Count > RequireNameBatchDocument.MaxWrites ||
            !tree.OrderSynchronized || !roster.Select(node => node.Node).SequenceEqual(tree.Order) ||
            roster.Any(node => node.Parent != tree.ParentOf(node.Node) || node.Name != tree.NameOf(node.Node)) ||
            !filter.IncludesSelection(tree, selected)) return null;
        var subjects = selected.ToHashSet();
        if (subjects.Count != selected.Count) return null;
        var slots = roster.Select((node, position) => (node.Node, position)).ToDictionary(p => p.Node, p => p.position);
        var targets = new List<BatchRenameTarget>(subjects.Count);
        var parents = new Stack<EntityGuid>(tree.ChildrenOf(null).Reverse());
        while (parents.TryPop(out var node))
        {
            foreach (var child in tree.ChildrenOf(node).Reverse()) parents.Push(child);
            if (!subjects.Contains(node) || tree.IncarnationOf(node) is not { } occurrence) continue;
            var path = new Stack<string>();
            for (var parent = tree.ParentOf(node); parent is { } id; parent = tree.ParentOf(id))
                path.Push(tree.NameOf(id) is { Length: > 0 } name ? name : "(unnamed)");
            targets.Add(new(node, tree.ParentOf(node), tree.NameOf(node), slots[node],
                path.Count == 0 ? "Scene root" : string.Join(" / ", path), occurrence));
        }
        return targets.Count == subjects.Count ? new(document, filter, targets.ToArray()) : null;
    }
}
