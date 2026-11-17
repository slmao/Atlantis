using System;
using System.Collections.Generic;
using System.Linq;
using Atlantis.Gameplay;

namespace Atlantis.Editor.Model;

public sealed record HierarchyComponent(TypeId? Id, string Label);

public sealed record HierarchySnapshot(long Lifetime, ulong Generation, IReadOnlyList<HierarchyRow> Rows,
    IReadOnlyList<HierarchyComponent> Components, IReadOnlyList<Marker> Markers);

public sealed record HierarchyFilter
{
    public static HierarchyFilter Empty { get; } = new("", null);

    public HierarchyFilter(string? name, TypeId? component)
    {
        Name = name?.Trim() ?? "";
        Component = component;
    }

    public string Name { get; }
    public TypeId? Component { get; }
    public bool Active => Name.Length != 0 || Component is not null;

    public bool EquivalentTo(HierarchyFilter other) =>
        Component == other.Component && string.Equals(Name, other.Name, StringComparison.OrdinalIgnoreCase);

    public bool Matches(string name, IReadOnlyList<TypeId> components) =>
        (Name.Length == 0 || name.Contains(Name, StringComparison.OrdinalIgnoreCase)) &&
        (Component is not TypeId type || components.Contains(type));

    private static HashSet<EntityGuid> IncludeAncestors(IEnumerable<EntityGuid> matches, Func<EntityGuid, EntityGuid?> parent)
    {
        var visible = new HashSet<EntityGuid>();
        foreach (EntityGuid match in matches)
            for (EntityGuid? node = match; node is EntityGuid id && visible.Add(id); node = parent(id)) { }
        return visible;
    }

    public HierarchyProjection Project(IReadOnlyList<HierarchyRow> rows)
    {
        if (!Active)
            return new(rows, rows.Select(row => row.Node).ToHashSet(), new());

        var parents = rows.ToDictionary(row => row.Node, row => row.Parent);
        var matches = rows.Where(row => Matches(row.Name, row.ComponentTypes)).Select(row => row.Node).ToHashSet();
        var visible = IncludeAncestors(matches, node => parents.GetValueOrDefault(node));
        return new(rows.Where(row => visible.Contains(row.Node)).ToArray(), visible, matches);
    }

    public bool IncludesSelection(DocumentTree tree, IReadOnlyList<EntityGuid> selection)
    {
        if (!Active) return true;
        var matches = tree.Order.Where(node => Matches(tree.NameOf(node), tree.ComponentsOf(node)));
        var visible = IncludeAncestors(matches, tree.ParentOf);
        return selection.All(visible.Contains);
    }

    public static IReadOnlyList<HierarchyComponent> ComponentChoices(IReadOnlyList<SchemaType> schema)
    {
        // The authoring schema includes child structs as well as top-level components.
        var nested = schema.SelectMany(type => type.Fields).Where(field => field.Kind == TypeKind.Struct)
            .Select(field => field.Type).ToHashSet();
        return schema.Where(type => type.Kind == TypeKind.Struct && !nested.Contains(type.Id))
            .OrderBy(type => type.ShortName, StringComparer.Ordinal)
            .Select(type => new HierarchyComponent(type.Id, type.ShortName)).ToArray();
    }
}

public sealed class HierarchyProjection
{
    private readonly HashSet<EntityGuid> _visible;
    private readonly HashSet<EntityGuid> _matches;

    internal HierarchyProjection(IReadOnlyList<HierarchyRow> rows, HashSet<EntityGuid> visible, HashSet<EntityGuid> matches)
    {
        Rows = rows;
        _visible = visible;
        _matches = matches;
    }

    public IReadOnlyList<HierarchyRow> Rows { get; }
    public int MatchCount => _matches.Count;
    public bool Matches(EntityGuid node) => _matches.Contains(node);
    public bool Visible(EntityGuid node) => _visible.Contains(node);

    public HierarchyRow? Locate(IReadOnlyList<HierarchyRow> full, IReadOnlyList<EntityGuid> selected) =>
        full.FirstOrDefault(row => selected.Contains(row.Node) && !Visible(row.Node)) ??
        full.FirstOrDefault(row => selected.Contains(row.Node));
}

public sealed class HierarchyScrollAnchor
{
    private readonly IReadOnlyList<HierarchyRow> _rows;
    private readonly int _position;

    public HierarchyScrollAnchor(IReadOnlyList<HierarchyRow> rows, HierarchyRow anchor)
    {
        _rows = rows;
        _position = -1;
        for (int i = 0; i < rows.Count; ++i)
        {
            if (!ReferenceEquals(rows[i], anchor)) continue;
            _position = i;
            break;
        }
    }

    public HierarchyRow? Restore(IReadOnlyList<HierarchyRow> rows)
    {
        var current = rows.ToDictionary(row => row.Incarnation);
        for (int i = _position; i >= 0; --i)
            if (current.TryGetValue(_rows[i].Incarnation, out HierarchyRow? row)) return row;
        return rows.FirstOrDefault();
    }
}
