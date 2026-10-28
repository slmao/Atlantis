// Plan 0068 M2 (Spec 0068 R3, R6, R7, Q5, Q6; Plan J4 as ruled; ADR-0135
// D3): the Hierarchy's model, the shared selection, the Hierarchy commands
// and the apply controller, against a fake document that applies the closed
// command set, validates as the session does (ParentExists, ParentAcyclic,
// ActiveCameraExists; all or nothing) and reports every change as an event.
using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
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
        public SortedSet<ulong> Components = new();
    }

    private List<(EntityGuid Id, Node Node)> _nodes = new();
    private Dictionary<SceneAddress, PropertyValue> _values = new();
    private EntityGuid? _camera;
    private readonly List<Event> _events = new();
    private readonly List<CommandFailure> _failures = new();
    private ulong _nextTicket = 1;
    private ulong _generation;

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

    public DocumentInfo? Info() => new(default, Provenance.HandAuthored, "fake.scene.txt", (ulong)_nodes.Count);

    public IReadOnlyList<NodeSummary> ListNodes() => _nodes.Select(n => new NodeSummary(n.Id, n.Node.Parent)).ToList();

    public IReadOnlyList<TypeId> ListComponents(EntityGuid node) =>
        _nodes.FirstOrDefault(n => n.Id == node).Node?.Components.Select(c => new TypeId(c)).ToList() ?? new List<TypeId>();

    public PropertyValue? GetProperty(SceneAddress address) => _values.TryGetValue(address, out PropertyValue v) ? v : null;

    public EntityGuid? ActiveCamera() => _camera;

    public TransactionTicket SubmitTransaction(IReadOnlyList<Command> commands)
    {
        ++Submissions;
        var first = new CommandTicket(_nextTicket);
        _nextTicket += (ulong)commands.Count;
        var nodes = _nodes.Select(n => (n.Id, new Node { Parent = n.Node.Parent, Components = new SortedSet<ulong>(n.Node.Components) })).ToList();
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
    public void Reload() => _events.Add(new Applied(++_generation));

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

        // Euler extraction round-trips through the World's composition.
        bool roundTrips = true;
        foreach (Vector3 euler in new[] { new Vector3(0.3f, -1.2f, 2.0f), new Vector3(-0.7f, 2.9f, -0.4f), new Vector3(1.4f, 0.2f, 0.3f) })
        {
            float[] m = WorldMatrices.ComposeLocal(Local(new(1, 2, 3), euler, new(-1, 2, 0.5f)));
            roundTrips &= TransformMath.ToLocalTransform(m) is LocalTransform t && NearMatrix(WorldMatrices.ComposeLocal(t), m, 1e-5f);
        }
        Check(roundTrips, "transform math: a matrix decomposes to a local Transform that composes back (reflection kept)");
    }

    private static void ApplyControllerChecks()
    {
        var document = new FakeDocument();
        var apply = new ApplyController(document);
        apply.Request();
        apply.Request();  // while one is in flight: waits
        apply.Request();
        Check(document.Applies == 1 && apply.Status == ApplyStatus.Applying && apply.Text == "applying", "apply: one in flight, later requests wait");
        document.Reload();
        foreach (Event e in document.DrainEvents()) apply.Apply(e);
        Check(document.Applies == 2 && apply.Status == ApplyStatus.Applying, "apply: at Applied, the waiting requests coalesce into one more");
        document.Reload();
        foreach (Event e in document.DrainEvents()) apply.Apply(e);
        Check(document.Applies == 2 && apply.Status == ApplyStatus.Applied && apply.Text == "applied · generation 2", "apply: settled, generation shown");
        apply.SetPaused(true);
        apply.Request();
        Check(apply.Status == ApplyStatus.PendingPaused && apply.Text.StartsWith("pending: paused"), "apply: under a pause the reload is pending");
        apply.SetPaused(false);
        document.Reload();
        foreach (Event e in document.DrainEvents()) apply.Apply(e);
        document.NextApplyError = ApplyError.CookFailed;
        apply.Request();
        Check(apply.Status == ApplyStatus.Failed && apply.Text.StartsWith("failed: CookFailed"), "apply: a refused apply is shown with its reason");
        apply.Request();
        Check(apply.Status == ApplyStatus.Applying, "apply: the next request retries");
    }
}
