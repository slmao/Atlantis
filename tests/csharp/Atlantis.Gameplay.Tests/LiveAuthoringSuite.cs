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
            ApplyUnderPause(t, first);
            Save(t, first);
        }
        Imported(t);
        NoAuthoring(t);
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
