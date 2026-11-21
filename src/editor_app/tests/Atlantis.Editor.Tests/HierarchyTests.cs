// Plan 0068 M2 (Spec 0068 R3, R6, R7, Q5, Q6; Plan J4 as ruled; ADR-0135
// D3): the Hierarchy's model, the shared selection, the Hierarchy commands
// and the apply controller, against a fake document that applies the closed
// command set, validates as the session does (ParentExists, ParentAcyclic,
// ActiveCameraExists; all or nothing) and reports every change as an event.
using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using System.Text;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Authoring;
using Command = Atlantis.Gameplay.Authoring.Command;
using CommandFailure = Atlantis.Gameplay.Authoring.CommandFailure;
using Event = Atlantis.Gameplay.Authoring.Event;
using EventFilter = Atlantis.Gameplay.Authoring.EventFilter;
using SetProperty = Atlantis.Gameplay.Authoring.SetProperty;
using AddComponent = Atlantis.Gameplay.Authoring.AddComponent;
using RemoveComponent = Atlantis.Gameplay.Authoring.RemoveComponent;
using PropertyChanged = Atlantis.Gameplay.Authoring.PropertyChanged;
using ComponentAdded = Atlantis.Gameplay.Authoring.ComponentAdded;
using ComponentRemoved = Atlantis.Gameplay.Authoring.ComponentRemoved;

namespace Atlantis.Editor.Tests;

/// <summary>An in-memory authoring document: the closed command set, validation, events and Applied.</summary>
internal sealed class FakeDocument : IAuthoringDocument
{
    public static readonly TypeId Transform = new(101), Light = new(102), CameraType = new(103), LightKind = new(104), CameraFog = new(105);
    public static readonly FieldId Position = new(1), Euler = new(2), ScaleField = new(3);
    // Plan 0068 M3: a Light's fields (an enum and an optional one), a Camera's nested fog and a read-only exposure.
    public static readonly FieldId Intensity = new(11), Color = new(12), Kind = new(13), Range = new(14), Fog = new(15), Exposure = new(16), Density = new(17);

    private sealed class Node
    {
        public EntityGuid? Parent;
        public string Name = "";
        public SortedSet<ulong> Components = new();
    }

    private List<(EntityGuid Id, Node Node)> _nodes = new();
    private Dictionary<SceneAddress, PropertyValue> _values = new();
    private EntityGuid? _camera;
    private readonly List<Event> _events = new();
    private readonly List<CommandFailure> _failures = new();
    private ulong _nextTicket = 1;
    private ulong _generation, _revision = 1;
    private ApplyReceipt? _pendingApply;
    private readonly Dictionary<ApplyId, ApplyRecord> _applies = new();
    private ApplyRecord? _newestApply;
    private ApplyProjection? _projection;
    public int StatusReads;
    public bool UnknownStatus;

    public ApplyError? NextApplyError;
    public int Applies;

    public IReadOnlyList<SchemaType> Schema { get; } = new[]
    {
        new SchemaType(Transform, "asset_system::scene::Transform", TypeKind.Struct, 1,
                       new[]
                       {
                           new SchemaField(Position, "localPosition", TypeKind.Primitive, PrimitiveKind.Vec3Float32, default, FieldFlags.Serializable),
                           new SchemaField(Euler, "localEulerAnglesRadians", TypeKind.Primitive, PrimitiveKind.Vec3Float32, default, FieldFlags.Serializable),
                           new SchemaField(ScaleField, "localScale", TypeKind.Primitive, PrimitiveKind.Vec3Float32, default, FieldFlags.Serializable),
                       },
                       Array.Empty<SchemaConstant>()),
        new SchemaType(Light, "asset_system::scene::Light", TypeKind.Struct, 1,
                       new[]
                       {
                           new SchemaField(Intensity, "intensity", TypeKind.Primitive, PrimitiveKind.Float32, default, FieldFlags.Serializable),
                           new SchemaField(Color, "color", TypeKind.Primitive, PrimitiveKind.Vec3Float32, default, FieldFlags.Serializable),
                           new SchemaField(Kind, "kind", TypeKind.Enum, default, LightKind, FieldFlags.Serializable),
                           new SchemaField(Range, "range", TypeKind.Primitive, PrimitiveKind.Float32, default, FieldFlags.Serializable | FieldFlags.Optional),
                       },
                       Array.Empty<SchemaConstant>()),
        new SchemaType(LightKind, "asset_system::scene::LightKind", TypeKind.Enum, 1, Array.Empty<SchemaField>(),
                       new[] { new SchemaConstant("Directional", 0), new SchemaConstant("Point", 1) }),
        new SchemaType(CameraType, "asset_system::scene::Camera", TypeKind.Struct, 1,
                       new[]
                       {
                           new SchemaField(Exposure, "exposureCompensationEv", TypeKind.Primitive, PrimitiveKind.Float32, default, FieldFlags.None),
                           new SchemaField(Fog, "fog", TypeKind.Struct, default, CameraFog, FieldFlags.Serializable),
                       },
                       Array.Empty<SchemaConstant>()),
        new SchemaType(CameraFog, "asset_system::scene::CameraFog", TypeKind.Struct, 1,
                       new[] { new SchemaField(Density, "density", TypeKind.Primitive, PrimitiveKind.Float32, default, FieldFlags.Serializable) },
                       Array.Empty<SchemaConstant>()),
    };

    public static EntityGuid Guid(int n) => EntityGuid.Parse($"00000000-0000-4000-8000-{n:D12}");

    /// <summary>Adds a node directly (the document as loaded), with a Transform holding <paramref name="local"/>.</summary>
    public EntityGuid Add(int n, EntityGuid? parent, LocalTransform local, params TypeId[] more)
    {
        EntityGuid id = Guid(n);
        var node = new Node { Parent = parent };
        node.Components.Add(Transform.Value);
        foreach (TypeId t in more) node.Components.Add(t.Value);
        foreach (TypeId t in more) SeedDefaults(id, t, _values);
        _nodes.Add((id, node));
        _values[new SceneAddress(id, Transform, Position)] = PropertyValue.FromVec3(local.Position);
        _values[new SceneAddress(id, Transform, Euler)] = PropertyValue.FromVec3(local.EulerRadians);
        _values[new SceneAddress(id, Transform, ScaleField)] = PropertyValue.FromVec3(local.Scale);
        return id;
    }

    public void SetCamera(EntityGuid? camera) => _camera = camera;

    // A component's fields at their defaults, as the real session adds one.
    private static void SeedDefaults(EntityGuid id, TypeId component, Dictionary<SceneAddress, PropertyValue> values)
    {
        if (component == Light)
        {
            values[new SceneAddress(id, Light, Intensity)] = PropertyValue.FromFloat32(1.0f);
            values[new SceneAddress(id, Light, Color)] = PropertyValue.FromVec3(Vector3.One);
            values[new SceneAddress(id, Light, Kind)] = PropertyValue.FromEnum(0);
            values[new SceneAddress(id, Light, Range)] = PropertyValue.Absent;
        }
        else if (component == CameraType)
        {
            values[new SceneAddress(id, CameraType, Exposure)] = PropertyValue.FromFloat32(0.0f);
            values[new SceneAddress(id, CameraType, Density)] = PropertyValue.FromFloat32(0.5f);
        }
    }

    public int Submissions { get; private set; }
    public int PropertyReads { get; private set; }
    public SceneAddress? Unreadable { get; set; }

    public DocumentInfo? Info() => new(default, Provenance.HandAuthored, "fake.scene.txt", (ulong)_nodes.Count);

    public IReadOnlyList<NodeSummary> ListNodes() => _nodes.Select(n => new NodeSummary(n.Id, n.Node.Parent, n.Node.Name)).ToList();

    public IReadOnlyList<TypeId> ListComponents(EntityGuid node) =>
        _nodes.FirstOrDefault(n => n.Id == node).Node?.Components.Select(c => new TypeId(c)).ToList() ?? new List<TypeId>();

    public PropertyValue? GetProperty(SceneAddress address)
    {
        ++PropertyReads;
        return address != Unreadable && _values.TryGetValue(address, out PropertyValue v) ? v : null;
    }

    public EntityGuid? ActiveCamera() => _camera;

    public TransactionTicket SubmitTransaction(IReadOnlyList<Command> commands)
    {
        ++Submissions;
        var first = new CommandTicket(_nextTicket);
        _nextTicket += (ulong)commands.Count;
        var nodes = _nodes.Select(n => (n.Id, new Node { Parent = n.Node.Parent, Name = n.Node.Name, Components = new SortedSet<ulong>(n.Node.Components) })).ToList();
        var values = new Dictionary<SceneAddress, PropertyValue>(_values);
        EntityGuid? camera = _camera;
        var events = new List<Event>();
        AuthoringError? refusal = null;
        foreach (Command command in commands)
        {
            switch (command)
            {
                case CreateNode c:
                {
                    var node = new Node();
                    node.Components.Add(Transform.Value);  // a new node has its Transform already, as the real session's
                    foreach (TypeId t in c.Components)
                    {
                        if (!node.Components.Add(t.Value)) refusal ??= AuthoringError.ComponentAlreadyPresent;
                    }
                    EntityGuid id = c.Node ?? new EntityGuid(System.Guid.NewGuid());
                    nodes.Insert((int)c.Position, (id, node));
                    {
                        values[new SceneAddress(id, Transform, Position)] = PropertyValue.FromVec3(Vector3.Zero);
                        values[new SceneAddress(id, Transform, Euler)] = PropertyValue.FromVec3(Vector3.Zero);
                        values[new SceneAddress(id, Transform, ScaleField)] = PropertyValue.FromVec3(Vector3.One);
                    }
                    events.Add(new NodeCreated(id, c.Position));
                    foreach (TypeId t in c.Components) events.Add(new ComponentAdded(id, t));  // none for the Transform
                    break;
                }
                case DestroyNode d:
                    if (nodes.RemoveAll(n => n.Id == d.Node) == 0) refusal ??= AuthoringError.UnknownNode;
                    events.Add(new NodeDestroyed(d.Node));
                    break;
                case SetParent p:
                {
                    int i = nodes.FindIndex(n => n.Id == p.Node);
                    if (i < 0) { refusal ??= AuthoringError.UnknownNode; break; }
                    nodes[i].Item2.Parent = p.Parent;
                    events.Add(new ParentChanged(p.Node, p.Parent));
                    break;
                }
                case SetNodeName named:
                {
                    int i = nodes.FindIndex(n => n.Id == named.Node);
                    if (i < 0) { refusal ??= AuthoringError.UnknownNode; break; }
                    if (!NodeNames.IsValid(named.Name)) { refusal ??= AuthoringError.InvalidNodeName; break; }
                    nodes[i].Item2.Name = named.Name;
                    events.Add(new NodeNameChanged(named.Node, named.Name));
                    break;
                }
                case SetProperty s:
                    values[s.Address] = s.Value;
                    events.Add(new PropertyChanged(s.Address, s.Value));
                    break;
                case AddComponent ac:
                {
                    int i = nodes.FindIndex(n => n.Id == ac.Node);
                    if (i < 0) { refusal ??= AuthoringError.UnknownNode; break; }
                    if (!nodes[i].Item2.Components.Add(ac.Component.Value)) { refusal ??= AuthoringError.ComponentAlreadyPresent; break; }
                    SeedDefaults(ac.Node, ac.Component, values);
                    events.Add(new ComponentAdded(ac.Node, ac.Component));
                    break;
                }
                case RemoveComponent rc:
                {
                    int i = nodes.FindIndex(n => n.Id == rc.Node);
                    if (i < 0) { refusal ??= AuthoringError.UnknownNode; break; }
                    if (rc.Component == Transform) { refusal ??= AuthoringError.ComponentRequired; break; }
                    if (!nodes[i].Item2.Components.Remove(rc.Component.Value)) { refusal ??= AuthoringError.ComponentMissing; break; }
                    foreach (SceneAddress a in values.Keys.Where(k => k.Node == rc.Node && k.Component == rc.Component).ToList()) values.Remove(a);
                    events.Add(new ComponentRemoved(rc.Node, rc.Component));
                    break;
                }
                case SetActiveCamera a:
                    camera = a.Camera;
                    events.Add(new ActiveCameraChanged(a.Camera));
                    break;
                default:
                    refusal ??= AuthoringError.UnknownComponentType;
                    break;
            }
        }
        var ids = nodes.Select(n => n.Id).ToHashSet();
        if (refusal is null && nodes.Any(n => n.Item2.Parent is EntityGuid parent && !ids.Contains(parent))) refusal = AuthoringError.ParentExists;
        if (refusal is null && camera is EntityGuid cam && !ids.Contains(cam)) refusal = AuthoringError.ActiveCameraExists;
        if (refusal is null)
        {
            foreach (var (id, _) in nodes)
            {
                var seen = new HashSet<EntityGuid>();
                for (EntityGuid? c = id; c is EntityGuid x; c = nodes.First(n => n.Id == x).Item2.Parent)
                {
                    if (!seen.Add(x)) { refusal = AuthoringError.ParentAcyclic; break; }
                }
            }
        }
        if (refusal is AuthoringError error)
        {
            _failures.Add(new CommandFailure(first, error));
        }
        else
        {
            if (commands.Count != 0) ++_revision;
            _nodes = nodes;
            _values = values;
            _camera = camera;
            _events.AddRange(events);
        }
        return new TransactionTicket(first, (ulong)commands.Count);
    }

    public IReadOnlyList<CommandFailure> DrainFailures()
    {
        var drained = _failures.ToList();
        _failures.Clear();
        return drained;
    }

    public IReadOnlyList<Event> DrainEvents()
    {
        var drained = _events.ToList();
        _events.Clear();
        return drained;
    }

    public ApplyError? Apply()
    {
        ++Applies;
        if (NextApplyError is ApplyError error)
        {
            NextApplyError = null;
            return error;
        }
        return null;
    }

    /// <summary>The Runtime's reload: Applied for the next generation.</summary>
    public void Reload()
    {
        ++_generation;
        if (_pendingApply is ApplyReceipt receipt)
        {
            var record = new ApplyRecord(receipt, ApplyState.Applied, _generation);
            _applies[receipt.Id] = record; _newestApply = record;
            _projection = new ApplyProjection(receipt, _generation); _pendingApply = null;
        }
        _events.Add(new Applied(_generation));
    }
    public void FailReload(ReloadError error)
    {
        if (_pendingApply is not ApplyReceipt receipt) throw new InvalidOperationException("no pending apply");
        var record = new ApplyRecord(receipt, ApplyState.Failed, Error: error);
        _applies[receipt.Id] = record; _newestApply = record; _pendingApply = null;
        // Deliberately no event: the product must poll.
    }
    public Outcome<ApplyReceipt, ApplyError> ApplyTracked()
    {
        ApplyError? error = Apply();
        if (error is ApplyError refused) return Outcome<ApplyReceipt, ApplyError>.Err(refused);
        var receipt = new ApplyReceipt(new ApplyId(System.Guid.NewGuid()), _revision);
        if (_pendingApply is ApplyReceipt old) _applies[old.Id] = new ApplyRecord(old, ApplyState.Superseded, SupersededBy: receipt.Id);
        _pendingApply = receipt; _newestApply = new ApplyRecord(receipt, ApplyState.Pending); _applies[receipt.Id] = _newestApply;
        return Outcome<ApplyReceipt, ApplyError>.Ok(receipt);
    }
    public Outcome<ApplySnapshot, ApplyLookupError> ApplyStatus(ApplyId? id = null)
    {
        ++StatusReads;
        if (UnknownStatus) return Outcome<ApplySnapshot, ApplyLookupError>.Err(ApplyLookupError.UnknownApply);
        ApplyRecord? record = id is ApplyId wanted ? _applies.GetValueOrDefault(wanted) : _newestApply;
        return id is not null && record is null ? Outcome<ApplySnapshot, ApplyLookupError>.Err(ApplyLookupError.UnknownApply)
            : Outcome<ApplySnapshot, ApplyLookupError>.Ok(new ApplySnapshot(_revision, record, _projection));
    }

    /// <summary>Another client's Open of the same source: the document is reread (ADR-0135 D4).</summary>
    public void Reopen() { ++_revision; _events.Add(new DocumentReopened(default)); }

    public SaveError? Save() => null;
}

public static partial class Program
{
    private static LocalTransform Local(Vector3 position, Vector3 euler, Vector3 scale) => new(position, euler, scale);

    // Submits a plan's transaction and feeds the events to the tree and the selection; returns the refusal, if any.
    private static AuthoringError? Submit(FakeDocument document, DocumentTree tree, Selection selection, EditPlan plan)
    {
        document.SubmitTransaction(plan.Commands);
        IReadOnlyList<CommandFailure> failures = document.DrainFailures();
        foreach (Event e in document.DrainEvents())
        {
            tree.Apply(e, document);
            selection.Apply(e, tree);
        }
        return failures.Count > 0 ? failures[0].Error : null;
    }

    private static bool NearMatrix(float[] a, float[] b, float tolerance)
    {
        for (int i = 0; i < 16; ++i)
        {
            if (MathF.Abs(a[i] - b[i]) > tolerance * MathF.Max(1.0f, MathF.Abs(b[i]))) return false;
        }
        return true;
    }

    private static void HierarchyChecks()
    {
        SiblingOrderChecks();
        BatchInspectorChecks();
        // The document: a root R with two children A (with a child A1) and B (the active camera), and a free node F.
        var document = new FakeDocument();
        EntityGuid r = document.Add(1, null, LocalTransform.Identity);
        EntityGuid a = document.Add(2, r, Local(new(1, 0, 0), Vector3.Zero, Vector3.One));
        EntityGuid a1 = document.Add(3, a, Local(new(0, 1, 0), Vector3.Zero, Vector3.One), FakeDocument.Light);
        EntityGuid b = document.Add(4, r, Local(new(0, 0, 5), new(0.1f, 0.2f, 0), Vector3.One), FakeDocument.CameraType);
        EntityGuid f = document.Add(5, null, Local(new(3, 2, 1), new(0.4f, 0.1f, 0.7f), new(1, 2, 1)));
        document.SetCamera(b);
        var tree = new DocumentTree();
        tree.Rebuild(document);
        var selection = new Selection();

        Check(tree.ChildrenOf(null).SequenceEqual(new[] { r, f }) && tree.ChildrenOf(r).SequenceEqual(new[] { a, b }) &&
              tree.DescendantsOf(r).SequenceEqual(new[] { a, a1, b }),
              "hierarchy: roots, children and descendants in the document's order");
        Check(tree.Label(a1, document.Schema) == "00000000 · Transform Light", "hierarchy: a GUID prefix and the component kinds, Transform first");

        selection.Select(a1);
        var order = tree.Order.ToList();
        Check(Submit(document, tree, selection, HierarchyCommands.Rename(tree, a1, "", "灯 · \"one\" \\")) is null &&
              tree.Label(a1, document.Schema) == "灯 · \"one\" \\ · 00000000 · Transform Light" && selection.Node == a1 && tree.Order.SequenceEqual(order),
              "names: labels preserve exact Unicode/quotes/slashes and GUID selection/order");
        string namedBefore = tree.NameOf(a1);
        Check(HierarchyCommands.Rename(tree, a1, "", "stale").Refusal == "changed elsewhere" && tree.NameOf(a1) == namedBefore,
              "names: an observed external rename refuses a stale draft");
        Check(HierarchyCommands.Rename(tree, a1, namedBefore, namedBefore).Refusal == "unchanged" &&
              HierarchyCommands.Rename(tree, a1, namedBefore, "bad\nname").Refusal == "InvalidNodeName",
              "names: identical text submits nothing and invalid text is refused");
        Check(Submit(document, tree, selection, HierarchyCommands.Rename(tree, a, "", namedBefore)) is null && tree.NameOf(a) == tree.NameOf(a1),
              "names: duplicate display names remain separate GUID nodes");
        Check(Submit(document, tree, selection, HierarchyCommands.Rename(tree, a1, namedBefore, "")) is null &&
              tree.Label(a1, document.Schema) == "00000000 · Transform Light", "names: empty restores the existing fallback label");

        // A CreateNode listing Transform is refused, as the real session does (the node has it already).
        Check(Submit(document, tree, selection, new EditPlan("dup", new Command[] { new CreateNode(null, (ulong)tree.Count, new[] { FakeDocument.Transform }) })) ==
              AuthoringError.ComponentAlreadyPresent, "fake document: a CreateNode listing the Transform is refused, as the real session's");

        // Create under A: one transaction, appended to the order, under A.
        var (create, created) = HierarchyCommands.CreateChild(document, tree, a);
        Check(Submit(document, tree, selection, create) is null && tree.Contains(created) && tree.ParentOf(created) == a &&
              tree.Order[^1] == created && tree.ComponentsOf(created).SequenceEqual(new[] { FakeDocument.Transform }) &&
              create.Commands.OfType<CreateNode>().Single().Components.Count == 0,
              "hierarchy: create appends a node with a Transform under the parent, in one transaction");

        // Destroy R's subtree while B (the active camera) is in it: the camera is cleared first, all in one transaction.
        selection.Select(a1);
        EditPlan destroy = HierarchyCommands.DestroySubtree(document, tree, r);
        Check(destroy.Commands[0] is SetActiveCamera { Camera: null } && destroy.Commands.Count(c => c is DestroyNode) == 5,
              "hierarchy: destroying a subtree destroys every descendant and clears the active camera among them");
        int before = document.Submissions;
        Check(Submit(document, tree, selection, destroy) is null && document.Submissions == before + 1 &&
              tree.ChildrenOf(null).SequenceEqual(new[] { f }) && document.ActiveCamera() is null,
              "hierarchy: the subtree is gone in one transaction");
        Check(selection.Node is null, "selection: a node destroyed (by any client) is deselected");

        // Destroying a node alone while its children stay is refused by the document (ParentExists): the subtree rule matters.
        var doc2 = new FakeDocument();
        EntityGuid p2 = doc2.Add(1, null, LocalTransform.Identity);
        doc2.Add(2, p2, LocalTransform.Identity);
        var tree2 = new DocumentTree();
        tree2.Rebuild(doc2);
        Check(Submit(doc2, tree2, new Selection(), new EditPlan("bare", new Command[] { new DestroyNode(p2) })) == AuthoringError.ParentExists &&
              tree2.Count == 2, "hierarchy: a bare DestroyNode orphaning a child is refused, nothing changes");

        // J4: under a parent scaled (1, 1, 0.25) and rotated, a rotated node's placement cannot be kept: refused, nothing submitted.
        var doc3 = new FakeDocument();
        EntityGuid squashed = doc3.Add(1, null, Local(new(2, 0, -1), new(0.3f, 0.5f, 0.2f), new(1, 1, 0.25f)));
        EntityGuid uniform = doc3.Add(2, null, Local(new(-1, 4, 2), new(0.2f, -0.6f, 0.3f), new(2, 2, 2)));
        EntityGuid child = doc3.Add(3, null, Local(new(5, 1, 3), new(0.4f, 0.1f, 0.7f), new(1, 1.5f, 1)));
        var tree3 = new DocumentTree();
        tree3.Rebuild(doc3);
        EditPlan refused = HierarchyCommands.Reparent(doc3, tree3, child, squashed);
        int submissions = doc3.Submissions;
        Check(refused.Refusal == HierarchyCommands.NotRepresentable && refused.Commands.Count == 0 && doc3.Submissions == submissions &&
              tree3.ParentOf(child) is null,
              "reparent (J4): under a parent scaled (1, 1, 0.25) and rotated, refused with its reason, the document unchanged");

        // Under a uniformly scaled, rotated parent: the world matrix is kept within the TRS tolerance.
        float[] worldBefore = TransformMath.WorldOf(doc3, tree3, child);
        EditPlan kept = HierarchyCommands.Reparent(doc3, tree3, child, uniform);
        Check(kept.Refusal is null && kept.Commands[0] is SetParent && Submit(doc3, tree3, new Selection(), kept) is null &&
              tree3.ParentOf(child) == uniform && NearMatrix(TransformMath.WorldOf(doc3, tree3, child), worldBefore, 1e-4f),
              "reparent (J4): under a uniformly scaled, rotated parent the node's world matrix is kept");
        // And back to the root: kept again.
        Check(Submit(doc3, tree3, new Selection(), HierarchyCommands.Reparent(doc3, tree3, child, null)) is null &&
              tree3.ParentOf(child) is null && NearMatrix(TransformMath.WorldOf(doc3, tree3, child), worldBefore, 1e-4f),
              "reparent: back to the root keeps the world matrix");
        // A node under its own descendant: refused client-side.
        Submit(doc3, tree3, new Selection(), HierarchyCommands.Reparent(doc3, tree3, child, uniform));
        Check(HierarchyCommands.Reparent(doc3, tree3, uniform, child).Refusal == "a node cannot be its own ancestor",
              "reparent: a node under its own descendant is refused");

        // Spec 0103: guard the captured source and prove that the same recorded
        // before/after values replay without changing descendant locals or document order.
        var moves = new FakeDocument();
        var original = moves.Add(20, null, Local(new(1, 3, 0), new(0.2f, 0, 0), new(2, 2, 2)));
        var moving = moves.Add(21, original, Local(new(2, 0, 1), new(0, 0.3f, 0), new(-1, 2, 1)));
        var descendant = moves.Add(22, moving, Local(new(0, 1, 1), new(0, 0, 0.4f), Vector3.One), FakeDocument.CameraType);
        var destination = moves.Add(23, null, Local(new(3, 2, 0), new(0.1f, 0.2f, 0), new(0.5f, 0.5f, 0.5f)));
        var singular = moves.Add(24, null, Local(Vector3.Zero, Vector3.Zero, new(1, 0, 1)));
        var invalid = moves.Add(25, null, Local(Vector3.Zero, Vector3.Zero, new(float.NaN, 1, 1)));
        moves.SetCamera(descendant);
        var moveTree = new DocumentTree();
        moveTree.Rebuild(moves);
        var moveOrder = moveTree.Order.ToArray();
        var priorWorld = TransformMath.WorldOf(moves, moveTree, moving);
        var descendantWorld = TransformMath.WorldOf(moves, moveTree, descendant);
        var descendantLocal = TransformMath.LocalOf(moves, descendant);
        moves.Unreadable = new SceneAddress(original, FakeDocument.Transform, FakeDocument.ScaleField);
        Check(HierarchyCommands.PlanReparent(moves, moveTree, moving, original, destination).Edit is null &&
              moves.Submissions == 0, "reparent: unreadable ancestor has no default fallback");
        moves.Unreadable = null;
        Check(HierarchyCommands.PlanReparent(moves, moveTree, moving, null, destination).Edit is null &&
              HierarchyCommands.PlanReparent(moves, moveTree, moving, original, original).Edit is null &&
              HierarchyCommands.PlanReparent(moves, moveTree, moving, original, FakeDocument.Guid(99)).Edit is null &&
              HierarchyCommands.PlanReparent(moves, moveTree, moving, original, singular).Edit is null &&
              HierarchyCommands.PlanReparent(moves, moveTree, moving, original, invalid).Edit is null &&
              moves.Submissions == 0, "reparent: stale source, same or missing parent, singular and non-finite inputs refuse");
        var nearLock = moves.Add(26, null, Local(Vector3.Zero, new(MathF.PI / 2 - 0.0003f, 0.2f, 0.1f), Vector3.One));
        var identityParent = moves.Add(27, null, LocalTransform.Identity);
        moveTree.Rebuild(moves);
        moveOrder = moveTree.Order.ToArray();
        Check(HierarchyCommands.PlanReparent(moves, moveTree, nearLock, null, identityParent).Edit is null &&
              moves.Submissions == 0, "reparent: an Euler recompose outside the world tolerance refuses");
        var edit = HierarchyCommands.PlanReparent(moves, moveTree, moving, original, destination).Edit ??
            throw new InvalidOperationException("no reparent plan for the reflected fixture");
        var sharedBatch = StructuralSteps.Commands(edit, true, moves.Schema);
        Check(sharedBatch.Count == 4 && sharedBatch[0] is SetParent && sharedBatch.Skip(1).All(command => command is SetProperty) &&
              Submit(moves, moveTree, new Selection(), new EditPlan(edit.Label, sharedBatch)) is null &&
              NearMatrix(TransformMath.WorldOf(moves, moveTree, moving), priorWorld, 1e-4f) &&
              NearMatrix(TransformMath.WorldOf(moves, moveTree, descendant), descendantWorld, 1e-4f) &&
              TransformMath.LocalOf(moves, descendant) == descendantLocal && moves.ActiveCamera() == descendant &&
              moveTree.Order.SequenceEqual(moveOrder),
              "reparent: one batch preserves reflected source, descendant placement and identities");
        var moved = TransformMath.LocalOf(moves, moving);
        var moveTarget = new AuthoringEditTarget(moves, moveTree, new ApplyController(moves));
        var moveHistory = new EditHistory(moveTarget);
        moveHistory.Record(edit, moveTarget.Committed(Array.Empty<CommandFailure>()));
        moveHistory.Update();
        Check(moveHistory.Undo() == HistoryResult.Submitted, "reparent: one Undo submits the recorded parent and local values");
        foreach (Event e in moves.DrainEvents()) moveTree.Apply(e, moves);
        moveHistory.Update();
        Check(moveTree.ParentOf(moving) == original && moveHistory.Redo() == HistoryResult.Submitted,
              "reparent: one Redo reuses the same identities");
        foreach (Event e in moves.DrainEvents()) moveTree.Apply(e, moves);
        moveHistory.Update();
        Check(moveTree.ParentOf(moving) == destination && TransformMath.LocalOf(moves, moving) == moved,
              "reparent: redo restores the exact recorded local Transform");
        Submit(moves, moveTree, new Selection(), new EditPlan("external move",
            [new SetProperty(edit.Writes[0].Address, PropertyValue.FromVec3(new Vector3(8, 7, 6)))]));
        int settled = moves.Submissions;
        Check(moveHistory.Undo() == HistoryResult.ChangedElsewhere && moves.Submissions == settled,
              "reparent: an external local edit refuses Undo without a submission");

        // Euler extraction round-trips through the World's composition.
        bool roundTrips = true;
        foreach (Vector3 euler in new[] { new Vector3(0.3f, -1.2f, 2.0f), new Vector3(-0.7f, 2.9f, -0.4f), new Vector3(1.4f, 0.2f, 0.3f) })
        {
            float[] m = WorldMatrices.ComposeLocal(Local(new(1, 2, 3), euler, new(-1, 2, 0.5f)));
            roundTrips &= TransformMath.ToLocalTransform(m) is LocalTransform t && NearMatrix(WorldMatrices.ComposeLocal(t), m, 1e-5f);
        }
        Check(roundTrips, "transform math: a matrix decomposes to a local Transform that composes back (reflection kept)");
    }

    private static void BatchInspectorChecks()
    {
        var document = new FakeDocument();
        var first = document.Add(1, null, LocalTransform.Identity, FakeDocument.Light);
        var second = document.Add(2, null, new LocalTransform(Vector3.Zero, Vector3.Zero, new Vector3(2)));
        var selection = new Selection();
        Check(selection.Set(new [] {first, second}) && selection.Node is null && selection.Nodes.Count == 2,
            "batch selection has no single target");
        ulong generation = selection.Generation;
        Check(!selection.Set(Enumerable.Range(1, 65).Select(FakeDocument.Guid)) && selection.Generation == generation,
            "selection beyond the cap preserves the existing selection");

        var tree = new DocumentTree();
        tree.Rebuild(document);
        var common = new InspectorModel(document).ReadCommon(selection.Nodes, tree)!;
        var scale = common.Single(item => item.Field.Path == "Transform.localScale");
        Check(common.All(item => item.Field.Component == FakeDocument.Transform) &&
            scale.State == CommonValueState.Mixed && scale.Field.Text == "" && scale.Field.Editable,
            "common fields intersect components and a mixed value is not input");
        var change = new SceneAddress(first, FakeDocument.Transform, FakeDocument.ScaleField);
        tree.Apply(new PropertyChanged(change, PropertyValue.FromVec3(new Vector3(2))), document);
        tree.Apply(new PropertyChanged(change, PropertyValue.FromVec3(Vector3.One)), document);
        Check(!scale.Targets[0].IsCurrent(tree, checkStamp: true) && scale.Targets[0].IsCurrent(tree, checkStamp: false),
            "a value changed away and back invalidates the draft but preserves its component incarnation");
        tree.Apply(new NodeDestroyed(second), document);
        tree.Apply(new NodeCreated(second, 1), document);
        Check(!scale.Targets[1].IsCurrent(tree, checkStamp: false), "same GUID replacement conflicts with replay");
        selection.Apply(new NodeDestroyed(second), tree);
        Check(selection.Node == first, "destroyed selected incarnation is removed");
    }

    private static void SiblingOrderChecks()
    {
        var document = new FakeDocument();
        var first = document.Add(1, null, LocalTransform.Identity);
        var child = document.Add(2, first, LocalTransform.Identity);
        var last = document.Add(3, null, LocalTransform.Identity);
        var tree = new DocumentTree();
        tree.Rebuild(document);
        object? identity = tree.IncarnationOf(first);
        Check(tree.AdjacentSibling(first, SiblingDirection.Down) == last &&
            tree.AdjacentSibling(first, SiblingDirection.Up) is null, "adjacency filters by parent, including root");
        Check(tree.Apply(new NodeOrderChanged(first, last, 2, 0, 2), document) &&
            tree.Order.SequenceEqual(new[] { last, child, first }) &&
            ReferenceEquals(identity, tree.IncarnationOf(first)), "order delta swaps only the two slots and keeps identity");
        tree.Apply(new NodeOrderChanged(first, last, 2, 0, 3), document);
        Check(!tree.OrderSynchronized, "inconsistent delta suspends order");
        var newChild = document.Add(4, last, LocalTransform.Identity);
        tree.Apply(new NodeCreated(newChild, 3), document);
        tree.Apply(new ParentChanged(newChild, last), document);
        object? childIdentity = tree.IncarnationOf(newChild);
        Check(!tree.Apply(new NodeMovedInSiblingOrder(last, null, 0, 2, 4), document) &&
            !tree.Apply(new NodeOrderChanged(last, first, 2, 0, 5), document),
            "an unsynchronized tree defers both order deltas pending a snapshot");
        Check(tree.ReconcileOrder(document.ListNodes()) && tree.OrderSynchronized &&
            tree.Order.SequenceEqual(new[] { first, child, last, newChild }) &&
            ReferenceEquals(identity, tree.IncarnationOf(first)) &&
            ReferenceEquals(childIdentity, tree.IncarnationOf(newChild)),
            "snapshot covers a queued lifecycle event once and retires both order deltas");
        Check(tree.Apply(new NodeMovedInSiblingOrder(first, null, 0, 2, 6), document) &&
            tree.Order.SequenceEqual(new[] { last, child, first, newChild }) &&
            ReferenceEquals(childIdentity, tree.IncarnationOf(newChild)),
            "new relative delta applies once after the snapshot");
    }

    private static void DuplicateSubtreeChecks()
    {
        var document = new FakeDocument();
        EntityGuid parent = document.Add(1, null, LocalTransform.Identity);
        EntityGuid child = document.Add(2, FakeDocument.Guid(3), LocalTransform.Identity, FakeDocument.CameraType);
        float negativeZero = BitConverter.Int32BitsToSingle(int.MinValue);
        var local = new LocalTransform(new Vector3(negativeZero, 3, 4), new Vector3(1, 2, 3), new Vector3(-2, 3, 1));
        EntityGuid source = document.Add(3, parent, local, FakeDocument.Light);
        EntityGuid sibling = document.Add(4, parent, LocalTransform.Identity);
        document.SetCamera(child);
        var tree = new DocumentTree();
        tree.Rebuild(document);
        var selection = new Selection();
        string longName = new string('a', 243) + "🌳🌳🌳";
        string firstCandidate = new string('a', 243) + "🌳 Copy";
        string expectedName = new string('a', 243) + "🌳 Copy 2";
        var naming = new EditPlan("name fixture", new Command[]
        {
            new SetNodeName(source, longName), new SetNodeName(sibling, firstCandidate),
            new SetNodeName(child, "camera · \"original\"")
        });
        Check(Submit(document, tree, selection, naming) is null, "duplicate: long-name fixture");

        document.Unreadable = new SceneAddress(source, FakeDocument.Light, FakeDocument.Range);
        Check(HierarchyCommands.DuplicateSubtree(document, tree, source).Copy is null,
            "duplicate: an unreadable optional field is distinct from Absent");
        document.Unreadable = null;
        SubtreeDuplicate copy = HierarchyCommands.DuplicateSubtree(document, tree, source).Copy ??
            throw new InvalidOperationException("the source should be duplicable");
        var nodes = copy.Edit.Structure.OfType<NodeLife>().Select(life => life.State).ToList();
        var cloneChild = nodes[0].Node;
        var cloneRoot = nodes[1].Node;
        IReadOnlyList<Command> commands = StructuralSteps.Commands(copy.Edit, true, document.Schema);
        Check(copy.Root == cloneRoot && cloneRoot != source && cloneChild != child &&
              nodes[0].Parent == cloneRoot && nodes[1].Parent == parent &&
              nodes[1].Name == expectedName && Encoding.UTF8.GetByteCount(nodes[1].Name) <= NodeNames.MaxUtf8Bytes &&
              nodes[0].Name == "camera · \"original\"" && commands.All(c => c is not SetActiveCamera),
            "duplicate: fresh IDs, original relative order, remapped parent, UTF-8 name and existing active camera");
        Check(Submit(document, tree, selection, new EditPlan(copy.Edit.Label, commands)) is null &&
              tree.Order.TakeLast(2).SequenceEqual(new[] { cloneChild, cloneRoot }) && tree.ChildrenOf(parent).Last() == cloneRoot &&
              document.ActiveCamera() == child &&
              copy.Matches(new DocumentView(document, tree).ReadCompleteNodes(new[] { cloneChild, cloneRoot })!),
            "duplicate: one batch preserves all intended state and appends the root after its siblings");
        var changed = new SetProperty(new SceneAddress(cloneRoot, FakeDocument.Transform, FakeDocument.Position),
            PropertyValue.FromVec3(new Vector3(0.0f, 3, 4)));
        Submit(document, tree, selection, new EditPlan("signed zero", new[] { changed }));
        Check(!copy.Matches(new DocumentView(document, tree).ReadCompleteNodes(new[] { cloneChild, cloneRoot })!),
            "duplicate: the postcondition distinguishes signed-zero payloads");
        Submit(document, tree, selection, new EditPlan("restore payload", new[]
        {
            changed with { Value = PropertyValue.FromVec3(local.Position) }
        }));

        var apply = new ApplyController(document);
        var target = new AuthoringEditTarget(document, tree, apply);
        var history = new EditHistory(target);
        history.Record(copy.Edit, target.Committed(Array.Empty<CommandFailure>()));
        history.Update();
        void Observe()
        {
            foreach (Event e in document.DrainEvents())
            {
                tree.Apply(e, document);
                apply.Apply(e);
            }
            history.Update();
        }
        Check(history.UndoDepth == 1 && history.Undo() == HistoryResult.Submitted,
            "duplicate: the whole copy has one undo");
        Observe();
        Check(!tree.Contains(cloneRoot) && !tree.Contains(cloneChild) && tree.Contains(source) &&
              history.Redo() == HistoryResult.Submitted, "duplicate: undo removes both copies, redo is available");
        Observe();
        Check(copy.Matches(new DocumentView(document, tree).ReadCompleteNodes(new[] { cloneChild, cloneRoot })!),
            "duplicate: redo preserves the same identities and payload");

        document.SetCamera(cloneChild);
        Check(history.Undo() == HistoryResult.Submitted, "duplicate: active-camera conflict is settled by the host");
        Observe();
        Check(tree.Contains(cloneRoot) && tree.Contains(cloneChild) && document.ActiveCamera() == cloneChild &&
              history.Notice.Contains("ActiveCameraExists", StringComparison.Ordinal),
            "duplicate: removing an externally activated camera is refused atomically");
        document.SetCamera(child);
        history.Record(copy.Edit, target.Committed(Array.Empty<CommandFailure>()));
        history.Update();
        Check(history.Undo() == HistoryResult.Submitted, "duplicate: a settled undo makes redo available");
        Observe();
        Check(history.CanRedo, "duplicate: redo branch exists before a new edit");
        history.DiscardRedo();
        Check(!history.CanRedo, "duplicate: an unrecordable new edit still invalidates redo");
        Submit(document, tree, selection, new EditPlan(copy.Edit.Label, commands));
        history.Record(copy.Edit, target.Committed(Array.Empty<CommandFailure>()));
        history.Update();

        EntityGuid external = FakeDocument.Guid(5);
        Submit(document, tree, selection, new EditPlan("external child", new Command[]
        {
            new CreateNode(external, (ulong)tree.Count, Array.Empty<TypeId>()), new SetParent(external, cloneRoot)
        }));
        int beforeRefusal = document.Submissions;
        Check(history.Undo() == HistoryResult.ChangedElsewhere && document.Submissions == beforeRefusal &&
              tree.Contains(external), "duplicate: undo refuses an external child without submitting");

        var limit = new FakeDocument();
        EntityGuid root = limit.Add(1, null, LocalTransform.Identity);
        for (int i = 2; i <= HierarchyCommands.MaxDuplicateNodes; ++i) limit.Add(i, root, LocalTransform.Identity);
        var limitTree = new DocumentTree();
        limitTree.Rebuild(limit);
        var atLimit = HierarchyCommands.DuplicateSubtree(limit, limitTree, root).Copy;
        Check(atLimit?.Edit.Structure.Count == 64 &&
              ((NodeLife)atLimit.Edit.Structure[0]).State is { Name: "Copy", Parent: null },
            "duplicate: 64 nodes including an unnamed scene root are eligible");
        limit.Add(65, root, LocalTransform.Identity);
        limitTree.Rebuild(limit);
        int reads = limit.PropertyReads;
        var tooLarge = HierarchyCommands.DuplicateSubtree(limit, limitTree, root);
        Check(tooLarge.Copy is null && tooLarge.Refusal!.Contains("64") && limit.PropertyReads == reads &&
              limit.Submissions == 0, "duplicate: 65 nodes are refused before field reads or submission");
    }

    private static void StaticGroupChecks()
    {
        var document = new FakeDocument();
        var existing = document.Add(1, null, new LocalTransform(Vector3.Zero, Vector3.Zero, Vector3.One));
        document.SubmitTransaction([new SetNodeName(existing, "storefront")]);
        var tree = new DocumentTree();
        tree.Rebuild(document);
        var guid = AssetGuid.Parse("00810081-0081-4081-8081-008100810081");
        var schema = document.Schema.Concat([new SchemaType(new TypeId(199), "asset_system::scene::Renderable", TypeKind.Struct, 1,
            new[] {
                new SchemaField(new FieldId(21), "meshAsset", TypeKind.Primitive, PrimitiveKind.AssetGuid, default, FieldFlags.Serializable),
                new SchemaField(new FieldId(22), "materialAsset", TypeKind.Primitive, PrimitiveKind.AssetGuid, default, FieldFlags.Serializable | FieldFlags.Optional)
            }, Array.Empty<SchemaConstant>())]).ToArray();
        var first = new Atlantis.Gameplay.Assets.StaticGroupNode(null, new Vector3(1, 2, 3), Vector3.Zero, Vector3.One, new(guid, guid));
        var second = new Atlantis.Gameplay.Assets.StaticGroupNode(0, Vector3.One, new Vector3(0, 0.5f, 0), new Vector3(2), new(guid, null));
        var group = new Atlantis.Gameplay.Assets.StaticGroup(guid, "storefront", [first, second]);
        var planned = HierarchyCommands.AddStaticGroup(schema, tree, group);
        Check(planned.Edit is { Copies.Count: 3 } && planned.Edit.Copies[0].State.Name == "storefront 2",
            "group adds one root with an ordinal name and the complete ordered group");
        if (planned.Edit is not { } edit) return;
        var ids = edit.Copies.Select(copy => copy.State.Node).ToArray();
        var commands = edit.Commands(true, 7, schema).ToArray();
        Check(commands[0] is RequireDuplicateBatchRevision &&
            commands.OfType<CreateNode>().Select(node => node.Node).SequenceEqual(ids.Select(id => (EntityGuid?)id)),
            "group append is revision-guarded and uses explicit ordered identities");
        Check(commands.TakeLast(2).SequenceEqual(new Command[] { new SetParent(ids[1], ids[0]), new SetParent(ids[2], ids[1]) }),
            "group preserves internal links after every node has been created");
        Check(commands.OfType<SetProperty>().Any(value => value.Address.Node == ids[2] &&
            value.Address.Field == new FieldId(22) && value.Value.Equals(PropertyValue.Absent)),
            "the absent material override is preserved");
        var undo = edit.Commands(false, 8, schema);
        Check(undo.Count == 4 && undo[0] is RequireDuplicateBatchRevision &&
            undo.OfType<DestroyNode>().Select(node => node.Node).SequenceEqual(ids.Reverse()),
            "one Undo removes the complete append");
        Check(edit.Commands(true, 9, schema).Skip(1).Select(c => Atlantis.Gameplay.Remote.Json.Write(Atlantis.Gameplay.Remote.Codec.Encode(c)))
            .SequenceEqual(commands.Skip(1).Select(c => Atlantis.Gameplay.Remote.Json.Write(Atlantis.Gameplay.Remote.Codec.Encode(c)))),
            "Redo reuses all GUIDs and values");
        var intent = new StaticGroupIntent(guid, 20);
        intent.Cancel();
        Check(!intent.Current, "a retired Assets intent cannot be reused for a queued add");

        var prefab = group with { Nodes = [second with { Parent = 1, Name = "Chair" }, first with { Name = "Original" }] };
        var instance = HierarchyCommands.AddStaticGroup(schema, tree, prefab, prefab: true).Edit;
        Check(instance is { Copies.Count: 2, IsPrefab: true },
            "prefab adds the two template nodes without an extra wrapper");
        if (instance is null) return;
        var root = instance.Roots.Single();
        var append = instance.Commands(true, 10, schema);
        Check(instance.Copies[0].State.Name == "Chair" && instance.Copies[0].State.Parent == root &&
            instance.Copies[1].State.Name == "storefront 2" && instance.Copies[1].State.PrefabOrigin == guid &&
            instance.Copies[1].State.Parent is null && append.OfType<SetPrefabOrigin>().Single() == new SetPrefabOrigin(root, guid) &&
            GuardedDuplicateEdit.ExpectedEvents(append).OfType<NodeCreated>().Single(node => node.Node == root).PrefabOrigin == guid,
            "prefab keeps order and child labels and verifies the root origin on creation");
        Check(instance.Commands(false, 11, schema).OfType<DestroyNode>().Count() == 2 &&
            instance.Commands(true, 12, schema).Skip(1).SequenceEqual(append.Skip(1)),
            "prefab Undo removes the block and Redo uses the same identities and source");
    }

    private static void GuardedDuplicateChecks()
    {
        var document = new FakeDocument();
        EntityGuid a = document.Add(1, null, LocalTransform.Identity);
        EntityGuid b = document.Add(2, null, LocalTransform.Identity);
        EntityGuid leaf = document.Add(3, FakeDocument.Guid(5), new LocalTransform(Vector3.Zero, Vector3.Zero, new(-2, 3, 1)));
        EntityGuid sibling = document.Add(4, null, LocalTransform.Identity);
        EntityGuid middle = document.Add(5, a, LocalTransform.Identity);
        var tree = new DocumentTree();
        tree.Rebuild(document);
        var selection = new Selection();
        var target = new AuthoringEditTarget(document, tree, new ApplyController(document));
        var history = new EditHistory(target);
        var first = HierarchyCommands.DuplicateSubtrees(document, tree, [b, leaf, a]).Edit!;
        Check(first.Copies.Count == 4 && first.Roots.Count == 2 &&
            first.Copies[2].State.Parent == first.Copies[3].State.Node &&
            first.Copies[0].State.Name == "Copy" && first.Copies[1].State.Name == "Copy 2",
            "batch duplicate deduplicates through an intermediate and preserves interleaved document order");
        Check(HierarchyCommands.DuplicateSubtrees(document, tree, [a, leaf]).Edit?.Roots.Count == 1,
            "multi-selection with one effective root retains the guarded path");

        BatchReplayResult Replay(GuardedDuplicateEdit edit, bool forward)
        {
            if (edit.Matches(tree, document, !forward, checkIncarnations: !forward) != true)
                return new(BatchReplayState.Drop, "conflict");
            var commands = edit.Commands(forward, 1, document.Schema).Skip(1).ToArray();
            var refusal = Submit(document, tree, selection, new EditPlan("batch duplicate", commands));
            if (refusal is not null || edit.Matches(tree, document, forward, checkIncarnations: false) != true)
                throw new InvalidOperationException("unexpected fake batch settlement");
            if (forward) edit.CaptureCopyIdentities(tree);
            return new(BatchReplayState.Committed, "settled");
        }
        void Record(GuardedDuplicateEdit edit)
        {
            Check(Replay(edit, true).State == BatchReplayState.Committed, "forward replay accepted");
            history.Record(new EditTransaction("duplicate", Array.Empty<PropertyWrite>(), new EditEntry[] { edit }),
                target.Committed(Array.Empty<CommandFailure>()));
            history.Update();
        }
        Record(first);
        var second = HierarchyCommands.DuplicateSubtrees(document, tree, [a, sibling]).Edit!;
        Record(second);
        var beforeReplay = tree.Order.ToArray();
        Check(history.Undo(duplicate: Replay) == HistoryResult.Submitted &&
            history.Undo(duplicate: Replay) == HistoryResult.Submitted, "both batches undo independently");
        Check(history.Redo(duplicate: Replay) == HistoryResult.Submitted &&
            history.Redo(duplicate: Replay) == HistoryResult.Submitted &&
            tree.Order.SequenceEqual(beforeReplay), "verified local recreation preserves dependent redo by GUID and slot");
        Check(!history.InvalidatePositional() && history.UndoDepth == 2,
            "legacy order invalidation leaves guarded entries for replay's own boundary check");
        EntityGuid latest = second.Copies[^1].State.Node;
        Submit(document, tree, selection, new EditPlan("replace endpoint", new Command[] {
            new DestroyNode(latest), new CreateNode(latest, (ulong)(tree.Count - 1), Array.Empty<TypeId>()) }));
        int submissions = document.Submissions;
        Check(history.Undo(duplicate: Replay) == HistoryResult.ChangedElsewhere && document.Submissions == submissions,
            "externally replaced copy refuses whole undo without sending");

        var limit = new FakeDocument();
        EntityGuid root1 = limit.Add(1, null, LocalTransform.Identity);
        EntityGuid root2 = limit.Add(2, null, LocalTransform.Identity);
        for (int i = 3; i <= 64; ++i) limit.Add(i, i % 2 == 0 ? root1 : root2, LocalTransform.Identity);
        var limitTree = new DocumentTree();
        limitTree.Rebuild(limit);
        Check(HierarchyCommands.DuplicateSubtrees(limit, limitTree, [root1, root2]).Edit?.Copies.Count == 64,
            "batch limit applies to the union");
        limit.Add(65, root1, LocalTransform.Identity);
        limitTree.Rebuild(limit);
        int reads = limit.PropertyReads;
        Check(HierarchyCommands.DuplicateSubtrees(limit, limitTree, [root1, root2]).Edit is null &&
            limit.PropertyReads == reads, "65 unique nodes refuse before reading leaves");
    }

    private static void ApplyControllerChecks()
    {
        var document = new FakeDocument();
        var apply = new ApplyController(document);
        apply.Request();
        apply.Request();  // while one is in flight: waits
        apply.Request();
        Check(document.Applies == 1 && apply.Status == ApplyStatus.Applying && apply.Text.StartsWith("applying", StringComparison.Ordinal), "apply: one in flight, later requests wait");
        document.Reload();
        foreach (Event e in document.DrainEvents()) apply.Apply(e);
        apply.Poll();
        Check(document.Applies == 2 && apply.Status == ApplyStatus.Applying, "apply: at Applied, the waiting requests coalesce into one more");
        document.Reload();
        foreach (Event e in document.DrainEvents()) apply.Apply(e);
        apply.Poll();
        Check(document.Applies == 2 && apply.Status == ApplyStatus.Applied && apply.Text.StartsWith("applied · generation 2", StringComparison.Ordinal), "apply: settled, generation shown");
        apply.SetPaused(true);
        apply.Request();
        Check(apply.Status == ApplyStatus.PendingPaused && apply.Text.StartsWith("pending: paused"), "apply: under a pause the reload is pending");
        apply.SetPaused(false);
        document.Reload();
        foreach (Event e in document.DrainEvents()) apply.Apply(e);
        apply.Poll();
        document.NextApplyError = ApplyError.CookFailed;
        apply.Request();
        Check(apply.Status == ApplyStatus.Failed && apply.Text.StartsWith("failed: CookFailed"), "apply: a refused apply is shown with its reason");
        apply.Request();
        Check(apply.Status == ApplyStatus.Applying, "apply: the next request retries");

        // J8: Commit is the commit and its apply as one; a refusal sends no apply; while one is in flight it coalesces.
        var doc2 = new FakeDocument();
        var apply2 = new ApplyController(doc2);
        var missing = new EntityGuid(Guid.NewGuid());
        Check(apply2.Commit(new Command[] { new DestroyNode(missing) }).Count == 1 && doc2.Applies == 0 && apply2.Status == ApplyStatus.Idle,
              "commit: a refused transaction sends no apply");
        Check(apply2.Commit(new Command[] { new CreateNode(null, 0UL, Array.Empty<TypeId>()) }).Count == 0 &&
              doc2.Applies == 1 && apply2.Status == ApplyStatus.Applying, "commit: an accepted transaction sends its apply with it");
        apply2.Commit(new Command[] { new CreateNode(null, 0UL, Array.Empty<TypeId>()) });
        Check(doc2.Applies == 1, "commit: while an apply is in flight the next one waits");
        doc2.Reload();
        foreach (Event e in doc2.DrainEvents()) apply2.Apply(e);
        apply2.Poll();
        Check(doc2.Applies == 2 && apply2.Status == ApplyStatus.Applying, "commit: the waiting apply is issued at Applied");
        doc2.Reload();
        foreach (Event e in doc2.DrainEvents()) apply2.Apply(e);
        apply2.Poll();
        doc2.NextApplyError = ApplyError.CookFailed;
        apply2.Commit(new Command[] { new CreateNode(null, 0UL, Array.Empty<TypeId>()) });
        Check(apply2.Status == ApplyStatus.Failed, "commit: a refused apply is shown as Failed");
    }
}
