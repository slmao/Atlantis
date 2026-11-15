// Plan 0065 M6 (Spec 0065 R11, Q8 G-b; ADR-0130 D3): the "live.authoring"
// suite, against atlantis_csharp_fixture_server -- the reflective
// AuthoringSession over authoring.*: open and the queries, commands and a
// transaction by type and field name, a refusal reported by ticket, a
// subscription with every client's events, apply pending under pause and
// Applied{generation} on the step that performs it (Spec 0065 third
// Correction), save, an imported scene's save refused, and a Runtime that
// serves no authoring session (NoAuthoring).
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Threading.Tasks;
using Atlantis.Gameplay.Remote;
using A = Atlantis.Gameplay.Authoring;

namespace Atlantis.Gameplay.Tests;

public static class LiveAuthoringSuite
{
    private static readonly EntityGuid Mesh = LiveConnectionSuite.Mesh;
    private static readonly EntityGuid Sun = LiveConnectionSuite.Sun;
    private static readonly EntityGuid Lamp = LiveConnectionSuite.Lamp;
    private static readonly EntityGuid Camera = LiveConnectionSuite.Camera;
    private static readonly EntityGuid Unknown = LiveConnectionSuite.Unknown;

    public static void Run(TestContext t)
    {
        using (var server = new FixtureServer(t))
        {
            using RemoteSession first = server.Attach();
            using RemoteSession second = server.Attach();
            Queries(t, first, server);
            Commands(t, first, second);
            Names(t, first, second);
            SiblingOrder(t, first, second);
            GuardedProperties(t, first, second);
            ApplyUnderPause(t, first);
            TrackedApplyUnderPause(t, first, second);
            Save(t, first);
        }
        Imported(t);
        NoAuthoring(t);
        LegacySiblingOrder(t, exactRefusal: true);
        LegacySiblingOrder(t, exactRefusal: false);
        CreateSceneUnknownOutcome(t, RemoteError.Disconnected, null);
        CreateSceneUnknownOutcome(t, RemoteError.ProtocolError, "scenes/saved/save_other/scene.scene.txt");
    }

    private static void GuardedProperties(TestContext t, RemoteSession first, RemoteSession second)
    {
        var a = first.Authoring;
        var b = second.Authoring;
        t.Check(a.Document()?.SupportsGuardedPropertyTransactions == true, "capable live document");
        var targetA = a.Address(Mesh, "Transform.localScale");
        var targetB = a.Address(Sun, "Transform.localScale");
        var beforeA = a.GetProperty(targetA).Value;
        var beforeB = a.GetProperty(targetB).Value;
        var next = PropertyValue.FromVec3(new System.Numerics.Vector3(2, 2, 2));
        ulong revision = a.ApplyStatus().Value!.CurrentDocumentRevision!.Value;
        var subscription = b.Subscribe(new A.EventFilter());
        var ticket = a.SubmitTransaction(new A.Command[] {
            new A.RequireDocumentRevision(revision), new A.SetProperty(targetA, next), new A.SetProperty(targetB, next) });
        t.Check(ticket.Count == 3 && a.DrainFailures().Count == 0, "two writes have one guarded ticket range");
        t.Check(b.DrainEvents(subscription).Value.SequenceEqual(new A.Event[] {
            new A.PropertyChanged(targetA, next), new A.PropertyChanged(targetB, next) }), "old event encoding covers both writes");
        t.Equal(revision + 1, a.ApplyStatus().Value!.CurrentDocumentRevision!.Value, "one guarded revision");
        ticket = a.SubmitTransaction(new A.Command[] { new A.RequireDocumentRevision(revision), new A.SetProperty(targetA, beforeA) });
        t.Check(a.DrainFailures().Single() == new A.CommandFailure(ticket.First, A.AuthoringError.PropertyBatchStaleRevision) &&
            b.DrainEvents(subscription).Value.Count == 0, "stale guard refuses without events");
        a.SubmitTransaction(new A.Command[] {
            new A.RequireDocumentRevision(revision + 1), new A.SetProperty(targetA, beforeA), new A.SetProperty(targetB, beforeB) });
        t.Check(a.DrainFailures().Count == 0 && a.GetProperty(targetA).Value == beforeA && a.GetProperty(targetB).Value == beforeB,
            "guarded undo can write per-target values");
        b.Unsubscribe(subscription);
    }

    private static void SiblingOrder(TestContext t, RemoteSession first, RemoteSession second)
    {
        var a = first.Authoring;
        var b = second.Authoring;
        var original = a.ListNodes().Value;
        var legacy = b.Subscribe(new A.EventFilter());
        var observed = b.Subscribe(new A.EventFilter(A.EventKind.AllWithSiblingOrder));
        t.Check(b.SupportsSiblingOrder(), "canonical subscription establishes sibling order support");
        ulong revision = a.ApplyStatus().Value!.CurrentDocumentRevision!.Value;
        var command = new A.SwapAdjacentSiblings(original[0].Node, original[1].Node, null, A.SiblingDirection.Down, revision);
        var ticket = a.SubmitTransaction(new A.Command[] { command });
        t.Check(ticket.First.Value != 0 && ticket.Count == 1 && a.DrainFailures().Count == 0, "shared SDK order transaction settles");
        t.Check(b.DrainEvents(legacy).Value.Count == 0 && b.ListNodes().Value[0] == original[1],
            "old filter sees no unknown event and fresh order remains readable");
        t.Check(b.DrainEvents(observed).Value.SequenceEqual(new A.Event[] {
            new A.NodeOrderChanged(command.Node, command.Sibling, 1, 0, revision + 1)
        }), "opted-in observer receives one order event with final slots and revision");
        var inverse = command with { Direction = A.SiblingDirection.Up, ExpectedRevision = revision + 1 };
        a.SubmitTransaction(new A.Command[] { inverse });
        t.Check(a.DrainFailures().Count == 0 && a.ListNodes().Value.SequenceEqual(original), "inverse restores source order");
        b.DrainEvents(observed);
        var relative = b.Subscribe(new A.EventFilter(A.EventKind.AllWithSiblingMove));
        t.Check(b.SupportsSiblingMove() && b.SupportsSiblingOrder(), "relative tier retains adjacent support");
        var lastRoot = Enumerable.Range(0, original.Count).Last(i => original[i].Parent is null);
        var move = new A.MoveSiblingRelative(original[0].Node, original[lastRoot].Node, null, A.SiblingPlacement.After, revision + 2);
        ticket = a.SubmitTransaction(new A.Command[] { move });
        t.Check(ticket.Count == 1 && a.DrainFailures().Count == 0, "SDK relative transaction settles");
        t.Check(b.DrainEvents(relative).Value.SequenceEqual(new A.Event[] {
            new A.NodeMovedInSiblingOrder(move.Node, null, 0, (ulong)lastRoot, revision + 3)
        }), "relative subscription receives one fixed-size order delta");
        t.Check(b.DrainEvents(observed).Value.Count == 0 && b.DrainEvents(legacy).Value.Count == 0 &&
            b.ListNodes().Value[lastRoot] == original[0], "older filters retain their event sets and can read the new order");
        a.Submit(move with { Anchor = original[1].Node, Placement = A.SiblingPlacement.Before, ExpectedRevision = revision + 3 });
        t.Check(a.DrainFailures().Count == 0 && b.ListNodes().Value.SequenceEqual(original), "relative inverse restores source order");
        b.Unsubscribe(relative);
        b.Unsubscribe(observed);
        b.Unsubscribe(legacy);
    }

    private static void LegacySiblingOrder(TestContext t, bool exactRefusal)
    {
        const string scene = "52052052-00aa-4052-8052-0000000000aa";
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        Task<bool> peer = Task.Run(() =>
        {
            using TcpClient socket = listener.AcceptTcpClient();
            socket.ReceiveTimeout = 5000;
            using var stream = socket.GetStream();
            using var reader = new StreamReader(stream);
            using var writer = new StreamWriter(stream) { AutoFlush = true, NewLine = "\n" };
            JsonValue Read() => Json.Parse(reader.ReadLine() ?? throw new EndOfStreamException());
            void Reply(JsonValue request, string key, JsonValue value) =>
                writer.WriteLine(Json.Write(JsonValue.NewObject().Set("id", request.Find("id")!).Set(key, value)));
            Reply(Read(), "result", JsonValue.NewObject().Set("scene", JsonValue.String(scene)));
            Reply(Read(), "result", Json.Parse("[]"));
            var probe = Read();
            bool all = probe.Find("method")?.AsString == "authoring.subscribe" &&
                probe.Find("params")?.Find("filter")?.Find("kinds")?.Items.Any(k => k.AsString == "NodeOrderChanged") == true;
            Reply(probe, "error", JsonValue.NewObject().Set("code", JsonValue.String("BadRequest"))
                .Set("message", JsonValue.String(exactRefusal ? "malformed or missing filter" : "wrong filter")));
            if (!exactRefusal) return all;
            var fallback = Read();
            bool legacy = fallback.Find("method")?.AsString == "authoring.subscribe" &&
                fallback.Find("params")?.Find("filter")?.Find("kinds")?.Items.All(k => k.AsString != "NodeOrderChanged") == true;
            Reply(fallback, "result", JsonValue.Number(3UL));
            var read = Read();
            Reply(read, "result", Json.Parse("{\"ok\":[]}"));
            return all && legacy && read.Find("method")?.AsString == "authoring.listNodes";
        });
        var info = new SessionInfo((ushort)((IPEndPoint)listener.LocalEndpoint).Port, "00112233445566778899aabbccddeeff",
            1, AssetGuid.Parse(scene));
        using RemoteSession client = RemoteSession.Connect(info, new RemoteOptions { ResponseTimeoutMilliseconds = 5000 });
        var filter = new A.EventFilter(A.EventKind.AllWithSiblingOrder);
        if (exactRefusal)
        {
            t.Equal(3UL, client.Authoring.Subscribe(filter).Value, "same-session legacy subscription succeeds");
            t.Check(!client.Authoring.SupportsSiblingOrder(), "legacy connection disables sibling order");
            t.Throws<NotSupportedException>(() => client.Authoring.Submit(new A.SwapAdjacentSiblings(Mesh, Sun, null,
                A.SiblingDirection.Down, 1)), "legacy swap is refused before wire submission");
            t.Check(client.Authoring.ListNodes().IsOk && client.Failure is null, "legacy connection remains usable");
        }
        else
        {
            t.Throws<RemoteException>(() => client.Authoring.Subscribe(filter), "unexpected probe refusal fails normally");
            t.Equal<RemoteError?>(RemoteError.ProtocolError, client.Failure, "unexpected probe reply cannot enable order");
        }
        t.Check(peer.WaitAsync(TimeSpan.FromSeconds(5)).GetAwaiter().GetResult(), "probe and optional fallback used the expected wire requests");
    }

    private static void CreateSceneUnknownOutcome(TestContext t, RemoteError expected, string? replySource)
    {
        const string name = "unknown_starter";
        const string source = "scenes/saved/save_unknown_starter/scene.scene.txt";
        const string scene = "52052052-00aa-4052-8052-0000000000aa";
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        Task<JsonValue> peer = Task.Run(() =>
        {
            using TcpClient socket = listener.AcceptTcpClient();
            socket.ReceiveTimeout = 5000;
            using var stream = socket.GetStream();
            using var reader = new StreamReader(stream);
            using var writer = new StreamWriter(stream) { AutoFlush = true, NewLine = "\n" };
            JsonValue Read() => Json.Parse(reader.ReadLine() ?? throw new EndOfStreamException());
            void Reply(JsonValue request, JsonValue result) =>
                writer.WriteLine(Json.Write(JsonValue.NewObject().Set("id", request.Find("id")!).Set("result", result)));

            Reply(Read(), JsonValue.NewObject().Set("scene", JsonValue.String(scene)));
            Reply(Read(), Json.Parse("[]"));
            JsonValue creation = Read();
            if (replySource is not null)
                Reply(creation, JsonValue.NewObject().Set("ok", JsonValue.NewObject()
                    .Set("scene", JsonValue.String(scene)).Set("source", JsonValue.String(replySource))));
            return creation;
        });

        var info = new SessionInfo((ushort)((IPEndPoint)listener.LocalEndpoint).Port,
            "00112233445566778899aabbccddeeff", 1, AssetGuid.Parse(scene));
        using RemoteSession client = RemoteSession.Connect(info, new RemoteOptions { ResponseTimeoutMilliseconds = 5000 });
        var error = t.Throws<RemoteException>(() => client.Authoring.CreateScene(name), $"{expected}: CreateScene throws");
        t.Equal<RemoteError?>(expected, error?.Error, "original error category");
        t.Equal<RemoteError?>(expected, client.Failure, "original error stays sticky");
        t.Check(error?.Message.Contains("outcome unknown", StringComparison.Ordinal) == true &&
            error.Message.Contains(source, StringComparison.Ordinal) &&
            error.Message.Contains("scene.scene.txt and catalog-source.txt", StringComparison.Ordinal) &&
            error.Message.Contains("Confirm the previous request can no longer commit before retrying", StringComparison.Ordinal),
            "direct SDK error gives destination and recovery guidance");
        var later = t.Throws<RemoteException>(() => client.Authoring.CreateScene("never_sent"), "failed session refuses a later call");
        t.Check(later is not null && ReferenceEquals(later, error?.InnerException) && later.Error == expected &&
            !later.Message.Contains("outcome unknown", StringComparison.Ordinal), "known unsent call has no unknown outcome");
        JsonValue request = peer.WaitAsync(TimeSpan.FromSeconds(5)).GetAwaiter().GetResult();
        t.Equal("authoring.createScene", request.Find("method")?.AsString, "peer received the CreateScene request");
        t.Equal(name, request.Find("params")?.Find("name")?.AsString, "peer received the attempted name");
    }

    private static void Names(TestContext t, RemoteSession first, RemoteSession second)
    {
        var a = first.Authoring;
        var b = second.Authoring;
        var subscription = b.Subscribe(new A.EventFilter(A.EventKind.NodeNameChanged, Mesh));
        const string name = "  名称 \"quoted\" \\ ";
        a.Submit(new A.SetNodeName(Mesh, name));
        t.Check(a.DrainFailures().Count == 0, "rename settles without refusal");
        t.Equal(name, b.ListNodes().Value.First(n => n.Node == Mesh).Name, "second client reads exact name");
        t.Check(b.DrainEvents(subscription).Value.Any(e => e is A.NodeNameChanged changed && changed.Name == name), "second client receives node-filtered rename");
        t.Equal<A.SaveError?>(null, a.Save(), "save name");
        t.Equal<A.OpenError?>(null, a.Open(), "reopen named source");
        t.Equal(name, a.ListNodes().Value.First(n => n.Node == Mesh).Name, "name persisted through save and reopen");
        a.Submit(new A.SetNodeName(Mesh, new string('x',256)));
        t.Check(a.DrainFailures().Any(f => f.Error == A.AuthoringError.InvalidNodeName), "invalid name uses shared refusal");
        t.Equal(name, a.ListNodes().Value.First(n => n.Node == Mesh).Name, "refusal changes no name");
        a.Submit(new A.SetNodeName(Mesh, ""));
        t.Check(a.DrainFailures().Count == 0, "clear name");
        b.Unsubscribe(subscription);
    }

    private static void Queries(TestContext t, RemoteSession session, FixtureServer server)
    {
        AuthoringSession a = session.Authoring;
        t.Equal<A.DocumentInfo?>(null, a.Document(), "no document before open");
        t.Equal(A.AuthoringError.NoDocument, a.ListNodes().Error, "a query before open: NoDocument");
        t.Equal<A.OpenError?>(null, a.Open(), "authoring.open succeeds");
        A.DocumentInfo document = a.Document()!;
        t.Check(document.Scene == server.Session.Scene && document.Provenance == A.Provenance.HandAuthored &&
                document.NodeCount == 5 && document.SourcePath.EndsWith("scene.scene.txt", StringComparison.Ordinal),
                "the document: the Runtime's scene, hand-authored, five nodes");

        IReadOnlyList<A.NodeSummary> nodes = a.ListNodes().Value;
        t.Equal(5, nodes.Count, "five nodes, in order");
        t.Equal(new A.NodeSummary(Lamp, Mesh), nodes[2], "the third node is the lamp, parented to the mesh node");
        t.Equal(new A.NodeSummary(Mesh, null), nodes[0], "the first node is a root");
        t.Equal(2, a.ListComponents(Lamp).Value.Count, "the lamp has Transform and Light");
        t.Equal(A.AuthoringError.UnknownNode, a.ListComponents(Unknown).Error, "an unknown node: UnknownNode");
        t.Equal<EntityGuid?>(Camera, a.ActiveCamera().Value, "the active camera");

        t.Check(a.Schema.Any(type => type.Name == "asset_system::scene::Light"), "authoring.schema: the scene component types");
        t.Equal(PropertyValue.FromFloat32(3.0f), a.GetProperty(Lamp, "Light.intensity").Value, "Light.intensity by name: 3");
        t.Equal(PropertyValue.FromFloat32(1.0472f), a.GetProperty(Camera, "Camera.fovYRadians").Value, "Camera.fovYRadians by name");
        t.Equal(A.AuthoringError.ComponentMissing, a.GetProperty(Mesh, "Light.intensity").Error,
                "a property of a component the node lacks: ComponentMissing");
        t.Equal(NameError.UnknownField,
                t.Throws<UnknownNameException>(() => a.Address(Lamp, "Light.brightness"), "an unknown field name throws")?.Error,
                "... UnknownField, nothing sent");
        t.Equal(NameError.UnknownType, t.Throws<UnknownNameException>(() => a.Type("Teleporter"), "an unknown type throws")?.Error,
                "... UnknownType");
    }

    private static void Commands(TestContext t, RemoteSession session, RemoteSession other)
    {
        AuthoringSession a = session.Authoring;
        AuthoringSession b = other.Authoring;
        SubscriptionId everything = b.Subscribe(new A.EventFilter());
        SubscriptionId lampOnly = b.Subscribe(new A.EventFilter(A.EventKind.PropertyChanged, Lamp, a.Type("Light")));

        A.SceneAddress intensity = a.Address(Lamp, "Light.intensity");
        CommandTicket ticket = a.Submit(new A.SetProperty(intensity, 7.0f));
        t.Check(ticket.Value > 0, "a submission takes a ticket");
        t.Check(a.DrainFailures().Count == 0, "... and is not refused");
        t.Equal(PropertyValue.FromFloat32(7.0f), b.GetProperty(intensity).Value, "the other client reads the change at once");
        t.Check(b.DrainEvents(everything).Value.SequenceEqual(new A.Event[] { new A.PropertyChanged(intensity, 7.0f) }),
                "the other client's subscription sees it");

        // A transaction is all or nothing: a refused one names its first command.
        TransactionTicket refused = a.SubmitTransaction(new A.Command[]
        {
            new A.SetProperty(intensity, 8.0f),
            new A.SetParent(Mesh, Lamp),  // the lamp is the mesh node's child
        });
        t.Equal(2UL, refused.Count, "a transaction of two takes two tickets");
        t.Check(a.DrainFailures().SequenceEqual(new[] { new A.CommandFailure(refused.First, A.AuthoringError.ParentAcyclic) }),
                "a refused transaction: one failure, ParentAcyclic, under its first ticket");
        t.Check(b.DrainFailures().Count == 0, "... reported to its submitter only");
        t.Equal(PropertyValue.FromFloat32(7.0f), a.GetProperty(intensity).Value, "... and nothing of it applied");
        t.Check(b.DrainEvents(everything).Value.Count == 0, "... nor any event");

        // A structural transaction: a minted node with a Camera, reparented.
        TypeId camera = a.Type("Camera");
        TransactionTicket created = a.SubmitTransaction(new A.Command[]
        {
            new A.CreateNode(null, 5, new[] { camera }),
        });
        t.Check(created.Count == 1 && a.DrainFailures().Count == 0, "CreateNode with a minted GUID");
        IReadOnlyList<A.Event> structural = b.DrainEvents(everything).Value;
        t.Check(structural.Count == 2 && structural[0] is A.NodeCreated { Position: 5 } &&
                structural[1] is A.ComponentAdded { Component: var added } && added == camera,
                "its events: NodeCreated at 5, then ComponentAdded Camera");
        EntityGuid minted = ((A.NodeCreated)structural[0]).Node;
        a.Submit(new A.SetParent(minted, Sun));
        a.Submit(new A.SetActiveCamera(minted));
        t.Check(a.DrainFailures().Count == 0, "reparent and make it the active camera");
        t.Equal<EntityGuid?>(minted, b.ActiveCamera().Value, "the other client sees the new active camera");
        t.Check(b.DrainEvents(everything).Value.SequenceEqual(new A.Event[]
                {
                    new A.ParentChanged(minted, Sun), new A.ActiveCameraChanged(minted),
                }), "ParentChanged and ActiveCameraChanged, in order");
        a.Submit(new A.SetActiveCamera(Camera));
        a.Submit(new A.DestroyNode(minted));
        t.Check(a.DrainFailures().Count == 0 && a.ListNodes().Value.Count == 5, "and destroy it again");

        t.Check(b.DrainEvents(lampOnly).Value.SequenceEqual(new A.Event[] { new A.PropertyChanged(intensity, 7.0f) }),
                "a filtered subscription: the lamp's Light changes only");
        t.Equal<ConnectionError?>(null, b.Unsubscribe(lampOnly), "unsubscribe");
        t.Equal<ConnectionError?>(ConnectionError.UnknownSubscription, b.Unsubscribe(lampOnly), "... twice: UnknownSubscription");
        t.Equal(ConnectionError.UnknownSubscription, b.DrainEvents(lampOnly).Error, "draining it: UnknownSubscription");
        b.Unsubscribe(everything);
    }

    // Spec 0065 third Correction: under pause an apply is accepted and stays
    // pending; the step performs it, and Applied{generation} reaches every
    // subscription.
    private static void ApplyUnderPause(TestContext t, RemoteSession session)
    {
        AuthoringSession a = session.Authoring;
        SubscriptionId applied = a.Subscribe(new A.EventFilter(A.EventKind.Applied));
        t.Equal(0UL, session.Control.Status().SceneGeneration, "generation 0 before any apply");
        session.Control.Pause();
        t.Equal<A.ApplyError?>(null, a.Apply(), "apply while paused: accepted");
        for (int i = 0; i < 5; ++i) session.Control.Status();  // frames pass, held
        t.Check(a.DrainEvents(applied).Value.Count == 0 && session.Control.Status().SceneGeneration == 0,
                "... and pending: no Applied, generation 0");
        StepResult step = session.Control.Step(new StepRequest(1));
        t.Check(step.Report is not null, "a step");
        t.Check(a.DrainEvents(applied).Value.SequenceEqual(new A.Event[] { new A.Applied(1) }), "... performs it: Applied{1}");
        t.Equal(1UL, session.Control.Status().SceneGeneration, "control.status: generation 1");
        session.Control.Resume();
        t.Equal<A.ApplyError?>(null, a.Apply(), "apply while running");
        long deadline = Environment.TickCount64 + 20000;
        IReadOnlyList<A.Event> events = Array.Empty<A.Event>();
        while (events.Count == 0 && Environment.TickCount64 < deadline) events = a.DrainEvents(applied).Value;
        t.Check(events.SequenceEqual(new A.Event[] { new A.Applied(2) }), "... Applied{2} at the next frame");
        a.Unsubscribe(applied);
    }

    private static void TrackedApplyUnderPause(TestContext t, RemoteSession first, RemoteSession second)
    {
        first.Control.Pause(); ulong generation = first.Control.Status().SceneGeneration;
        A.ApplyReceipt a = first.Authoring.ApplyTracked().Value!;
        A.ApplyReceipt b = second.Authoring.ApplyTracked().Value!;
        t.Check(a.Id != b.Id && a.DocumentRevision == b.DocumentRevision, "tracked equal revision requests have distinct identities");
        var old = first.Authoring.ApplyStatus(a.Id).Value!;
        t.Check(old.Request?.State == A.ApplyState.Superseded && old.Request.SupersededBy == b.Id, "SDK sees precise replacement while paused");
        t.Check(second.Authoring.ApplyStatus(b.Id).Value!.Request?.State == A.ApplyState.Pending, "SDK newest accepted receipt is held");
        first.Control.Step(new StepRequest(1));
        var after = first.Authoring.ApplyStatus(b.Id).Value!;
        t.Check(after.Request?.State == A.ApplyState.Applied && after.Request.Generation == generation + 1 && after.Projection?.Receipt == b,
                "SDK matching applied result agrees with projection and one Step");
        t.Equal(after, second.Authoring.ApplyStatus(b.Id).Value!, "second SDK client sees identical retained status");
        t.Check(first.Authoring.ApplyStatus(new A.ApplyId(Guid.NewGuid())).Error == A.ApplyLookupError.UnknownApply, "unknown is an in-band lookup refusal");
        first.Control.Resume();
    }

    private static void Save(TestContext t, RemoteSession session)
    {
        AuthoringSession a = session.Authoring;
        t.Equal<A.SaveError?>(null, a.Save(), "save a hand-authored document");
        string text = File.ReadAllText(a.Document()!.SourcePath);
        t.Check(text.Contains("intensity=7 range=2.5", StringComparison.Ordinal), "the saved source carries the edit");
        t.Equal<A.OpenError?>(null, a.Open(), "open again: the saved document reads back");
        t.Equal(PropertyValue.FromFloat32(7.0f), a.GetProperty(Lamp, "Light.intensity").Value, "... with the edit");
    }

    private static void Imported(TestContext t)
    {
        using var server = new FixtureServer(t, authoring: "imported");
        using RemoteSession session = server.Attach();
        AuthoringSession a = session.Authoring;
        t.Equal<A.OpenError?>(null, a.Open(), "an imported scene opens");
        t.Equal(A.Provenance.Imported, a.Document()?.Provenance, "... as imported");
        a.Submit(new A.SetProperty(a.Address(Lamp, "Light.intensity"), 5.0f));
        t.Check(a.DrainFailures().Count == 0, "... is editable");
        t.Equal<A.SaveError?>(A.SaveError.ImportedSceneNotSavable, a.Save(), "... and never saved: ImportedSceneNotSavable");
    }

    private static void NoAuthoring(TestContext t)
    {
        using var server = new FixtureServer(t, authoring: "none");
        using RemoteSession session = server.Attach();
        RemoteException? refused = t.Throws<RemoteException>(() => session.Authoring.Document(),
                                                            "a Runtime serving no authoring session refuses authoring.*");
        t.Check(refused?.Error == RemoteError.ProtocolError && refused.Message.Contains("NoAuthoring", StringComparison.Ordinal),
                "... NoAuthoring, as a ProtocolError (as NoControl is)");
        t.Equal<RemoteError?>(RemoteError.ProtocolError, session.Failure, "... and the session has failed");
    }
}
