// Plan 0068 M5 (Spec 0068 R8; ADR-0136): the structural records of the
// editor's history -- a node's life (create, destroy: its full state), a
// component's life, the active camera and a node's parent -- beside E2's
// property writes (EditHistory's PropertyWrite). A record is an ordered list
// of entries; applying one is a transaction built from the authoring
// document's closed command set (CreateNode, SetProperty for every
// serializable value, SetParent; DestroyNode; AddComponent/RemoveComponent;
// SetActiveCamera), and a step is refused when the document no longer holds
// what the edit left there (D3: the comparison, extended per variant). The
// records are the editor's own history, not a copy of the document: a node's
// values exist in them only from the destroy that removed it. A new C# model
// with no C++ reference (ADR-0137 D2), tested against a fake document.
// UI-free.
using System;
using System.Collections.Generic;
using System.Linq;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Authoring;

namespace Atlantis.Editor.Model;

/// <summary>One component of one node with every serializable value, in the schema's order.</summary>
public sealed record ComponentState(TypeId Type, IReadOnlyList<(SceneAddress Address, PropertyValue Value)> Values)
{
    public bool SameAs(ComponentState other) =>
        Type == other.Type && Values.Count == other.Values.Count &&
        Values.Zip(other.Values).All(p => p.First.Address == p.Second.Address && p.First.Value.Equals(p.Second.Value));
}

/// <summary>A node: its place in the order, its parent and children, and each component's state (sorted by type).</summary>
public sealed record NodeState(EntityGuid Node, ulong Position, EntityGuid? Parent, IReadOnlyList<EntityGuid> Children,
                               IReadOnlyList<ComponentState> Components)
{
    /// <summary>The same parent, children (as a set), components and values; the position is not compared (D3: it shifts).</summary>
    public bool SameAs(NodeState other) =>
        Parent == other.Parent && Children.Count == other.Children.Count && !Children.Except(other.Children).Any() &&
        Components.Count == other.Components.Count && Components.Zip(other.Components).All(p => p.First.SameAs(p.Second));
}

/// <summary>One entry of a structural record; the variants are ADR-0136 D1's.</summary>
public abstract record EditEntry;

/// <summary>A node created (absent before, present after) or destroyed (present before, absent after).</summary>
public sealed record NodeLife(NodeState State, bool Created) : EditEntry;

/// <summary>A component added or removed, with its values.</summary>
public sealed record ComponentLife(EntityGuid Node, ComponentState State, bool Added) : EditEntry;

public sealed record ActiveCameraEntry(EntityGuid? Before, EntityGuid? After) : EditEntry;

public sealed record ParentEntry(EntityGuid Node, EntityGuid? Before, EntityGuid? After) : EditEntry;

/// <summary>What a step needs to read of the document to compare it with a record (ADR-0136 D3).</summary>
public interface IDocumentView
{
    ulong NodeCount { get; }

    bool Exists(EntityGuid node);

    EntityGuid? ParentOf(EntityGuid node);

    EntityGuid? ActiveCamera();

    /// <summary>The node as the document holds it now; null when it is absent.</summary>
    NodeState? ReadNode(EntityGuid node);

    /// <summary>The component's values; null when the node does not have it.</summary>
    ComponentState? ReadComponent(EntityGuid node, TypeId component);
}

/// <summary>The view over a live document: structure from the tree (current as of the last poll), values read through the wire.</summary>
public sealed class DocumentView : IDocumentView
{
    private readonly IAuthoringDocument _document;
    private readonly DocumentTree _tree;
    private readonly Dictionary<TypeId, IReadOnlyList<SchemaField>> _leaves = new();

    public DocumentView(IAuthoringDocument document, DocumentTree tree)
    {
        _document = document;
        _tree = tree;
    }

    public ulong NodeCount => (ulong)_tree.Count;

    public bool Exists(EntityGuid node) => _tree.Contains(node);

    public EntityGuid? ParentOf(EntityGuid node) => _tree.ParentOf(node);

    public EntityGuid? ActiveCamera() => _document.ActiveCamera();

    public NodeState? ReadNode(EntityGuid node) => ReadNodes(new[] { node })[0];

    public ComponentState? ReadComponent(EntityGuid node, TypeId component) =>
        _tree.Contains(node) && _tree.ComponentsOf(node).Contains(component) ? ReadComponents(new[] { (node, component) })[0] : null;

    /// <summary>Several nodes in one pipelined read; null for an absent one.</summary>
    public IReadOnlyList<NodeState?> ReadNodes(IReadOnlyList<EntityGuid> nodes)
    {
        var wanted = new List<(EntityGuid Node, TypeId Type)>();
        foreach (EntityGuid node in nodes)
        {
            if (_tree.Contains(node)) wanted.AddRange(_tree.ComponentsOf(node).OrderBy(t => t.Value).Select(t => (node, t)));
        }
        IReadOnlyList<ComponentState> states = ReadComponents(wanted);
        var positions = new Dictionary<EntityGuid, int>();
        for (int i = 0; i < _tree.Order.Count; ++i) positions[_tree.Order[i]] = i;
        var result = new List<NodeState?>(nodes.Count);
        int next = 0;
        foreach (EntityGuid node in nodes)
        {
            if (!_tree.Contains(node))
            {
                result.Add(null);
                continue;
            }
            int count = _tree.ComponentsOf(node).Count;
            result.Add(new NodeState(node, (ulong)positions[node], _tree.ParentOf(node), _tree.ChildrenOf(node).ToList(),
                                     states.Skip(next).Take(count).ToList()));
            next += count;
        }
        return result;
    }

    /// <summary>Components' serializable values in one pipelined read (the node need not be in the tree).</summary>
    public IReadOnlyList<ComponentState> ReadComponents(IReadOnlyList<(EntityGuid Node, TypeId Type)> wanted)
    {
        var addresses = new List<SceneAddress>();
        var spans = new List<(int Start, int Count)>();
        foreach (var (node, type) in wanted)
        {
            int start = addresses.Count;
            foreach (SchemaField leaf in LeavesOf(type)) addresses.Add(new SceneAddress(node, type, leaf.Id));
            spans.Add((start, addresses.Count - start));
        }
        IReadOnlyList<PropertyValue?> values = _document.GetProperties(addresses);
        var states = new List<ComponentState>(wanted.Count);
        for (int i = 0; i < wanted.Count; ++i)
        {
            var read = new List<(SceneAddress, PropertyValue)>();
            for (int j = spans[i].Start; j < spans[i].Start + spans[i].Count; ++j)
            {
                if (values[j] is PropertyValue value) read.Add((addresses[j], value));
            }
            states.Add(new ComponentState(wanted[i].Type, read));
        }
        return states;
    }

    private IReadOnlyList<SchemaField> LeavesOf(TypeId type)
    {
        if (!_leaves.TryGetValue(type, out IReadOnlyList<SchemaField>? leaves))
        {
            _leaves[type] = leaves = SchemaText.LeavesOf(_document.Schema, type).Select(l => l.Leaf)
                                                .Where(f => (f.Flags & FieldFlags.Serializable) != 0).ToList();
        }
        return leaves;
    }
}

/// <summary>Comparing a record with the document (D3) and turning a step into commands (D2).</summary>
public static class StructuralSteps
{
    /// <summary>Null when the document holds what the edit left; else why the step is refused (nothing is submitted).</summary>
    public static HistoryResult? Check(EditTransaction edit, bool forward, IDocumentView view)
    {
        // The nodes this step creates, ascending by position: a parent among them need not exist yet, and each position
        // counts the nodes created before it.
        List<NodeState> creating = edit.Structure.OfType<NodeLife>().Where(n => n.Created == forward).Select(n => n.State)
                                       .OrderBy(s => s.Position).ToList();
        var creatingSet = creating.Select(s => s.Node).ToHashSet();
        foreach (EditEntry entry in edit.Structure)
        {
            switch (entry)
            {
                case NodeLife life when life.Created != forward:  // the node should be there, as the edit left it
                {
                    NodeState? now = view.ReadNode(life.State.Node);
                    if (now is null) return HistoryResult.Invalidated;
                    if (!life.State.SameAs(now)) return HistoryResult.ChangedElsewhere;
                    break;
                }
                case NodeLife life:  // the node should be absent
                    if (view.Exists(life.State.Node)) return HistoryResult.ChangedElsewhere;
                    break;
                case ComponentLife component:
                {
                    if (!view.Exists(component.Node)) return HistoryResult.Invalidated;
                    ComponentState? now = view.ReadComponent(component.Node, component.State.Type);
                    if (component.Added != forward)
                    {
                        if (now is null || !component.State.SameAs(now)) return HistoryResult.ChangedElsewhere;
                    }
                    else if (now is not null)
                    {
                        return HistoryResult.ChangedElsewhere;
                    }
                    break;
                }
                case ActiveCameraEntry camera:
                    if (view.ActiveCamera() != (forward ? camera.Before : camera.After)) return HistoryResult.ChangedElsewhere;
                    break;
                case ParentEntry parent:
                {
                    if (!view.Exists(parent.Node)) return HistoryResult.Invalidated;
                    if (view.ParentOf(parent.Node) != (forward ? parent.Before : parent.After)) return HistoryResult.ChangedElsewhere;
                    if ((forward ? parent.After : parent.Before) is EntityGuid target && !view.Exists(target)) return HistoryResult.Invalidated;
                    break;
                }
            }
        }
        for (int i = 0; i < creating.Count; ++i)
        {
            if (creating[i].Parent is EntityGuid parent && !creatingSet.Contains(parent) && !view.Exists(parent)) return HistoryResult.Invalidated;
            if (creating[i].Position > view.NodeCount + (ulong)i) return HistoryResult.ChangedElsewhere;
        }
        return null;
    }

    /// <summary>
    /// One step as one transaction: a redo writes the entries forward and then the property writes' After values; an
    /// undo writes the Before values and then the entries in reverse. Nodes to re-create come in ascending position.
    /// </summary>
    public static IReadOnlyList<Command> Commands(EditTransaction edit, bool forward, IReadOnlyList<SchemaType> schema)
    {
        TypeId? transform = SceneSchema.TypeOf(schema, "Transform");
        var commands = new List<Command>();
        if (!forward) AddWrites(commands, edit, forward);
        bool created = false;
        foreach (EditEntry entry in forward ? edit.Structure : edit.Structure.Reverse())
        {
            switch (entry)
            {
                case NodeLife life when life.Created != forward:
                    commands.Add(new DestroyNode(life.State.Node));
                    break;
                case NodeLife:
                    if (created) break;
                    created = true;
                    foreach (NodeState state in edit.Structure.OfType<NodeLife>().Where(n => n.Created == forward).Select(n => n.State)
                                                    .OrderBy(s => s.Position))
                    {
                        commands.Add(new CreateNode(state.Node, state.Position,
                                                    state.Components.Where(c => c.Type != transform).Select(c => c.Type).ToList()));
                        foreach (ComponentState component in state.Components) AddValues(commands, component);
                        if (state.Parent is EntityGuid parent) commands.Add(new SetParent(state.Node, parent));
                    }
                    break;
                case ComponentLife component when component.Added == forward:
                    commands.Add(new AddComponent(component.Node, component.State.Type));
                    AddValues(commands, component.State);
                    break;
                case ComponentLife component:
                    commands.Add(new RemoveComponent(component.Node, component.State.Type));
                    break;
                case ActiveCameraEntry camera:
                    commands.Add(new SetActiveCamera(forward ? camera.After : camera.Before));
                    break;
                case ParentEntry parent:
                    commands.Add(new SetParent(parent.Node, forward ? parent.After : parent.Before));
                    break;
            }
        }
        if (forward) AddWrites(commands, edit, forward);
        return commands;
    }

    private static void AddWrites(List<Command> commands, EditTransaction edit, bool forward)
    {
        foreach (PropertyWrite write in edit.Writes) commands.Add(new SetProperty(write.Address, forward ? write.After : write.Before));
    }

    private static void AddValues(List<Command> commands, ComponentState component)
    {
        foreach (var (address, value) in component.Values) commands.Add(new SetProperty(address, value));
    }
}

/// <summary>
/// Builds a record from a planned edit: before the commit, the values and states it will overwrite or remove are read;
/// after it, the states of what it created. A SetProperty on a node or component the same edit creates is folded into
/// that entry's state.
/// </summary>
public sealed class EditRecorder
{
    private abstract record Draft;
    private sealed record WriteDraft(SceneAddress Address, PropertyValue After) : Draft;
    private sealed record CreateDraft(EntityGuid Node, ulong Position, IReadOnlyList<TypeId> Extra, EntityGuid? Parent) : Draft;
    private sealed record DestroyDraft(EntityGuid Node) : Draft;
    private sealed record AddDraft(EntityGuid Node, TypeId Type) : Draft;
    private sealed record RemoveDraft(EntityGuid Node, TypeId Type) : Draft;
    private sealed record CameraDraft(EntityGuid? Before, EntityGuid? After) : Draft;
    private sealed record ParentDraft(EntityGuid Node, EntityGuid? Before, EntityGuid? After) : Draft;

    private readonly IAuthoringDocument _document;
    private readonly DocumentView _view;
    private readonly string _label;
    private List<Draft> _drafts = new();
    private IReadOnlyList<PropertyValue?> _before = Array.Empty<PropertyValue?>();
    private IReadOnlyList<NodeState?> _destroyed = Array.Empty<NodeState?>();
    private IReadOnlyList<ComponentState> _removed = Array.Empty<ComponentState>();

    private EditRecorder(IAuthoringDocument document, DocumentTree tree, string label)
    {
        _document = document;
        _view = new DocumentView(document, tree);
        _label = label;
    }

    /// <summary>Reads the pre-state of <paramref name="plan"/>; null when it cannot be recorded (then it is not undoable).</summary>
    public static EditRecorder? Begin(IAuthoringDocument document, DocumentTree tree, EditPlan plan)
    {
        var recorder = new EditRecorder(document, tree, plan.Label);
        var created = new HashSet<EntityGuid>();
        var added = new HashSet<(EntityGuid, TypeId)>();
        var createdAt = new Dictionary<EntityGuid, int>();
        EntityGuid? camera = null;
        bool cameraRead = false;
        foreach (Command command in plan.Commands)
        {
            switch (command)
            {
                case SetProperty set:
                    if (!created.Contains(set.Address.Node) && !added.Contains((set.Address.Node, set.Address.Component)))
                    {
                        recorder._drafts.Add(new WriteDraft(set.Address, set.Value));
                    }
                    break;
                case CreateNode { Node: EntityGuid node } create:
                    created.Add(node);
                    createdAt[node] = recorder._drafts.Count;
                    recorder._drafts.Add(new CreateDraft(node, create.Position, create.Components, null));
                    break;
                case CreateNode:
                    return null;
                case SetParent move when createdAt.TryGetValue(move.Node, out int index):
                    recorder._drafts[index] = ((CreateDraft)recorder._drafts[index]) with { Parent = move.Parent };
                    break;
                case SetParent move:
                    recorder._drafts.Add(new ParentDraft(move.Node, tree.ParentOf(move.Node), move.Parent));
                    break;
                case DestroyNode destroy:
                    recorder._drafts.Add(new DestroyDraft(destroy.Node));
                    break;
                case AddComponent add:
                    added.Add((add.Node, add.Component));
                    recorder._drafts.Add(new AddDraft(add.Node, add.Component));
                    break;
                case RemoveComponent remove:
                    recorder._drafts.Add(new RemoveDraft(remove.Node, remove.Component));
                    break;
                case SetActiveCamera set:
                    if (!cameraRead)
                    {
                        camera = document.ActiveCamera();
                        cameraRead = true;
                    }
                    recorder._drafts.Add(new CameraDraft(camera, set.Camera));
                    camera = set.Camera;
                    break;
                default:
                    return null;
            }
        }
        recorder._before = document.GetProperties(recorder._drafts.OfType<WriteDraft>().Select(w => w.Address).ToList());
        if (recorder._before.Any(v => v is null)) return null;
        recorder._destroyed = recorder._view.ReadNodes(recorder._drafts.OfType<DestroyDraft>().Select(d => d.Node).ToList());
        if (recorder._destroyed.Any(s => s is null)) return null;
        var removals = recorder._drafts.OfType<RemoveDraft>().Select(r => (r.Node, r.Type)).ToList();
        recorder._removed = recorder._view.ReadComponents(removals);
        return recorder;
    }

    /// <summary>The record, after the plan's commit settled; reads what the edit created.</summary>
    public EditTransaction Finish(IReadOnlyList<SchemaType> schema)
    {
        TypeId? transform = SceneSchema.TypeOf(schema, "Transform");
        var creations = _drafts.OfType<CreateDraft>().SelectMany(c => (transform is TypeId t ? new[] { t } : Array.Empty<TypeId>()).Concat(c.Extra)
                                                                    .Select(type => (c.Node, type))).ToList();
        var additions = _drafts.OfType<AddDraft>().Select(a => (a.Node, a.Type)).ToList();
        IReadOnlyList<ComponentState> created = _view.ReadComponents(creations.Concat(additions).ToList());
        var writes = new List<PropertyWrite>();
        var entries = new List<EditEntry>();
        int write = 0, destroyed = 0, removed = 0, createdRead = 0, addedRead = creations.Count;
        foreach (Draft draft in _drafts)
        {
            switch (draft)
            {
                case WriteDraft w:
                    writes.Add(new PropertyWrite(w.Address, _before[write++]!.Value, w.After));
                    break;
                case CreateDraft c:
                {
                    int count = (transform is null ? 0 : 1) + c.Extra.Count;
                    entries.Add(new NodeLife(new NodeState(c.Node, c.Position, c.Parent, Array.Empty<EntityGuid>(),
                                                           created.Skip(createdRead).Take(count).ToList()), true));
                    createdRead += count;
                    break;
                }
                case DestroyDraft:
                    entries.Add(new NodeLife(_destroyed[destroyed++]!, false));
                    break;
                case AddDraft a:
                    entries.Add(new ComponentLife(a.Node, created[addedRead++], true));
                    break;
                case RemoveDraft r:
                    entries.Add(new ComponentLife(r.Node, _removed[removed++], false));
                    break;
                case CameraDraft camera:
                    entries.Add(new ActiveCameraEntry(camera.Before, camera.After));
                    break;
                case ParentDraft parent:
                    entries.Add(new ParentEntry(parent.Node, parent.Before, parent.After));
                    break;
            }
        }
        return new EditTransaction(_label, writes, entries);
    }
}

/// <summary>
/// The history's boundary over the live authoring document (ADR-0124 D4, ADR-0135 D4): reads go through the document,
/// writes through the apply controller's commit -- so each undo and redo is a transaction and then an apply -- and a
/// commit settles as soon as its failures are drained (ADR-0130 D1). It reports no invalidations: a record naming what
/// is gone is caught at its step by the comparison (ADR-0136 D3).
/// </summary>
public sealed class AuthoringEditTarget : IEditTarget
{
    private readonly IAuthoringDocument _document;
    private readonly DocumentTree _tree;
    private readonly ApplyController _apply;
    private readonly DocumentView _view;
    private readonly Dictionary<ulong, (Settlement Settlement, string Text)> _handles = new();
    private ulong _next = 1;

    public AuthoringEditTarget(IAuthoringDocument document, DocumentTree tree, ApplyController apply)
    {
        _document = document;
        _tree = tree;
        _apply = apply;
        _view = new DocumentView(document, tree);
    }

    public IDocumentView? View => _view;

    public IReadOnlyList<SchemaType> Schema => _document.Schema;

    /// <summary>The handle of a commit its maker already made (an Inspector commit, a Gizmo release, a Hierarchy edit).</summary>
    public EditHandle Committed(IReadOnlyList<CommandFailure> failures)
    {
        ulong id = _next++;
        _handles[id] = failures.Count == 0 ? (Settlement.Applied, "") : (Settlement.Refused, failures[0].Error.ToString());
        return new EditHandle(id, 1);
    }

    public PropertyValue? Read(SceneAddress address) =>
        _tree.Contains(address.Node) && _tree.ComponentsOf(address.Node).Contains(address.Component) ? _document.GetProperty(address) : null;

    public EditHandle Submit(IReadOnlyList<(SceneAddress Address, PropertyValue Value)> writes) =>
        SubmitCommands(writes.Select(w => (Command)new SetProperty(w.Address, w.Value)).ToList());

    public EditHandle SubmitCommands(IReadOnlyList<Command> commands) => Committed(_apply.Commit(commands));

    public void Track(EditHandle handle, IReadOnlyList<(SceneAddress Address, PropertyValue Value)> writes)
    {
    }

    public Settlement SettlementOf(EditHandle handle) => _handles.TryGetValue(handle.First, out var h) ? h.Settlement : Settlement.Pending;

    public string RefusalText(EditHandle handle) => _handles.TryGetValue(handle.First, out var h) ? h.Text : "";

    public void Update()
    {
    }

    public IReadOnlyList<Invalidation> DrainInvalidations() => Array.Empty<Invalidation>();
}
