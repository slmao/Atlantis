// Plan 0068 M2 (Spec 0068 R3, Fact 1, Fact 8; Q6): the Hierarchy's model --
// the authoring document's nodes as a tree under their parents, in the
// document's order, each labelled by its GUID's first eight hex digits and
// its components' kinds (the scene grammar has no node names yet; Spec
// 0069). Built from ListNodes()/ListComponents() and kept current by every
// client's authoring events (no polling). A new C# model with no C++
// reference: ADR-0137 D2, tested against a fake document. UI-free.
using System;
using System.Collections.Generic;
using System.Linq;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Authoring;

namespace Atlantis.Editor.Model;

public sealed class DocumentTree
{
    private sealed class Row
    {
        public EntityGuid? Parent;
        public List<TypeId> Components = new();
    }

    private readonly Dictionary<EntityGuid, Row> _rows = new();
    private readonly List<EntityGuid> _order = new();  // the document's order
    // The children of every parent (and the roots), in the document's order; rebuilt on first use after a change.
    private Dictionary<EntityGuid, List<EntityGuid>>? _children;
    private List<EntityGuid>? _roots;

    /// <summary>Increments with every change, for a view to know when to refresh.</summary>
    public ulong Generation { get; private set; }

    public int Count => _order.Count;

    public IReadOnlyList<EntityGuid> Order => _order;

    public bool Contains(EntityGuid node) => _rows.ContainsKey(node);

    public EntityGuid? ParentOf(EntityGuid node) => _rows.TryGetValue(node, out Row? row) ? row.Parent : null;

    public IReadOnlyList<TypeId> ComponentsOf(EntityGuid node) =>
        _rows.TryGetValue(node, out Row? row) ? row.Components : Array.Empty<TypeId>();

    /// <summary>The children of <paramref name="parent"/> (the roots for null), in the document's order.</summary>
    public IReadOnlyList<EntityGuid> ChildrenOf(EntityGuid? parent)
    {
        if (_children is null || _roots is null)
        {
            _children = new Dictionary<EntityGuid, List<EntityGuid>>();
            _roots = new List<EntityGuid>();
            foreach (EntityGuid node in _order)
            {
                if (_rows[node].Parent is EntityGuid p)
                {
                    if (!_children.TryGetValue(p, out List<EntityGuid>? list)) _children[p] = list = new List<EntityGuid>();
                    list.Add(node);
                }
                else
                {
                    _roots.Add(node);
                }
            }
        }
        if (parent is not EntityGuid key) return _roots;
        return _children.TryGetValue(key, out List<EntityGuid>? children) ? children : Array.Empty<EntityGuid>();
    }

    /// <summary>The node's descendants, depth first, each child before its own children, in the document's order.</summary>
    public IReadOnlyList<EntityGuid> DescendantsOf(EntityGuid node)
    {
        var result = new List<EntityGuid>();
        var stack = new Stack<EntityGuid>(ChildrenOf(node).Reverse());
        while (stack.Count > 0)
        {
            EntityGuid next = stack.Pop();
            result.Add(next);
            foreach (EntityGuid child in ChildrenOf(next).Reverse()) stack.Push(child);
        }
        return result;
    }

    /// <summary>Whether <paramref name="ancestor"/> is <paramref name="node"/> or one of its ancestors.</summary>
    public bool IsSelfOrAncestor(EntityGuid ancestor, EntityGuid node)
    {
        EntityGuid? current = node;
        var seen = new HashSet<EntityGuid>();
        while (current is EntityGuid c && seen.Add(c))
        {
            if (c == ancestor) return true;
            current = ParentOf(c);
        }
        return false;
    }

    /// <summary>"2c5aa0de · Transform Camera": the GUID's first eight hex digits and the components' kinds.</summary>
    public string Label(EntityGuid node, IReadOnlyList<SchemaType> schema)
    {
        string prefix = node.Value.ToString("N")[..8];
        IEnumerable<string> kinds = ComponentsOf(node).Select(c => SceneSchema.NameOf(schema, c))
                                                      .OrderBy(k => k == "Transform" ? 0 : 1).ThenBy(k => k, StringComparer.Ordinal);
        return prefix + " · " + string.Join(" ", kinds);
    }

    /// <summary>Re-reads the whole document (on open and on DocumentReopened).</summary>
    public void Rebuild(IAuthoringDocument document)
    {
        _rows.Clear();
        _order.Clear();
        IReadOnlyList<NodeSummary> nodes = document.ListNodes();
        IReadOnlyList<IReadOnlyList<TypeId>> components = document.ListComponents(nodes.Select(n => n.Node).ToList());
        for (int i = 0; i < nodes.Count; ++i)
        {
            _order.Add(nodes[i].Node);
            _rows[nodes[i].Node] = new Row { Parent = nodes[i].Parent, Components = components[i].ToList() };
        }
        Changed();
    }

    private void Changed()
    {
        _children = null;
        _roots = null;
        ++Generation;
    }

    /// <summary>Follows one authoring event; returns whether the tree changed.</summary>
    public bool Apply(Event e, IAuthoringDocument document)
    {
        switch (e)
        {
            case NodeCreated created:
            {
                int position = (int)Math.Min(created.Position, (ulong)_order.Count);
                _order.Insert(position, created.Node);
                // A node is created with its Transform (a required component, no ComponentAdded for it); the rest arrive as events.
                var fresh = new Row();
                if (SceneSchema.TypeOf(document.Schema, "Transform") is TypeId transform) fresh.Components.Add(transform);
                _rows[created.Node] = fresh;
                break;
            }
            case NodeDestroyed destroyed:
                if (!_rows.Remove(destroyed.Node)) return false;
                _order.Remove(destroyed.Node);
                break;
            case ParentChanged moved:
                if (!_rows.TryGetValue(moved.Node, out Row? row)) return false;
                row.Parent = moved.Parent;
                break;
            case ComponentAdded added:
                if (!_rows.TryGetValue(added.Node, out Row? withAdded) || withAdded.Components.Contains(added.Component)) return false;
                withAdded.Components.Add(added.Component);
                withAdded.Components.Sort((a, b) => a.Value.CompareTo(b.Value));
                break;
            case ComponentRemoved removed:
                if (!_rows.TryGetValue(removed.Node, out Row? withRemoved) || !withRemoved.Components.Remove(removed.Component)) return false;
                break;
            case DocumentReopened:
                Rebuild(document);
                return true;
            default:
                return false;
        }
        Changed();
        return true;
    }
}

/// <summary>
/// The one selection the Viewport pick, the Hierarchy and the Inspector share (Spec 0068 Q6, S-a). A node any client
/// destroys is deselected; a selection persists across applies (GUIDs persist).
/// </summary>
public sealed class Selection
{
    public EntityGuid? Node { get; private set; }

    /// <summary>Increments with every change.</summary>
    public ulong Generation { get; private set; }

    public void Select(EntityGuid? node)
    {
        if (node == Node) return;
        Node = node;
        ++Generation;
    }

    /// <summary>Follows one authoring event: the selected node destroyed, or the document reopened without it.</summary>
    public void Apply(Event e, DocumentTree tree)
    {
        if (e is NodeDestroyed destroyed && destroyed.Node == Node) Select(null);
        if (e is DocumentReopened && Node is EntityGuid n && !tree.Contains(n)) Select(null);
    }
}

public enum ApplyStatus
{
    Idle,
    Applying,
    Applied,
    PendingPaused,
    Failed,
}

/// <summary>
/// ADR-0135 D3: every settled commit requests an apply, coalesced -- while one is in flight a further request waits and
/// is issued at its Applied (the newest document wins; each apply cooks the whole document). The state is always
/// shown.
/// </summary>
public sealed class ApplyController
{
    private readonly IAuthoringDocument _document;
    private bool _inFlight;
    private bool _again;
    private bool _paused;

    public ApplyController(IAuthoringDocument document) => _document = document;

    public ApplyStatus Status { get; private set; } = ApplyStatus.Idle;
    public ulong Generation { get; private set; }
    public ApplyError? LastError { get; private set; }

    /// <summary>The number of applies issued (for measurements and tests).</summary>
    public int Issued { get; private set; }

    public string Text => Status switch
    {
        ApplyStatus.Applying => "applying",
        ApplyStatus.Applied => "applied · generation " + Generation,
        ApplyStatus.PendingPaused => "pending: paused (applies on Step or Resume)",
        ApplyStatus.Failed => "failed: " + LastError + " (the document is unchanged; the next edit retries)",
        _ => "",
    };

    public void Request()
    {
        if (_inFlight)
        {
            _again = true;
            return;
        }
        Issue();
    }

    /// <summary>Records an apply the caller already sent (pipelined with its commit): in flight, as if Request() had issued it.</summary>
    public void NoteRequested()
    {
        ++Issued;
        _inFlight = true;
        Status = _paused ? ApplyStatus.PendingPaused : ApplyStatus.Applying;
    }

    /// <summary>The Runtime's pause state: a pause holds the reload (ADR-0131, amended 2026-10-23).</summary>
    public void SetPaused(bool paused)
    {
        _paused = paused;
        if (_inFlight) Status = paused ? ApplyStatus.PendingPaused : ApplyStatus.Applying;
    }

    /// <summary>Follows one authoring event: Applied settles the apply in flight and issues a waiting one.</summary>
    public void Apply(Event e)
    {
        if (e is not Applied applied) return;
        Generation = applied.Generation;
        _inFlight = false;
        Status = ApplyStatus.Applied;
        if (_again)
        {
            _again = false;
            Issue();
        }
    }

    private void Issue()
    {
        ++Issued;
        ApplyError? error = _document.Apply();
        if (error is not null)
        {
            LastError = error;
            Status = ApplyStatus.Failed;
            return;
        }
        _inFlight = true;
        Status = _paused ? ApplyStatus.PendingPaused : ApplyStatus.Applying;
    }
}
