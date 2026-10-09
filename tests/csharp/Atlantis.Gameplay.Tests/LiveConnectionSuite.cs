// Plan 0058 P9 (Spec 0058 R1, R6; J4): the "live.connection" suite, against
// atlantis_csharp_fixture_server -- every connection.* and control.* method
// through RemoteSession, the hello's refusals, framing (the line limit, CRLF),
// pipelining (answers matched by id), and a session failing once and for all.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Text;
using Atlantis.Gameplay.Remote;
using W = Atlantis.Gameplay.World;

namespace Atlantis.Gameplay.Tests;

public static class LiveConnectionSuite
{
    public static readonly EntityGuid Mesh = EntityGuid.Parse("52052052-0001-4052-8052-000000000001");
    public static readonly EntityGuid Sun = EntityGuid.Parse("52052052-0002-4052-8052-000000000002");
    public static readonly EntityGuid Lamp = EntityGuid.Parse("52052052-0003-4052-8052-000000000003");
    public static readonly EntityGuid Camera = EntityGuid.Parse("52052052-0004-4052-8052-000000000004");
    public static readonly EntityGuid Empty = EntityGuid.Parse("52052052-0005-4052-8052-000000000005");
    public static readonly EntityGuid Unknown = EntityGuid.Parse("58005800-0000-4000-8000-0000000000ff");

    private static PropertyAddress Intensity(EntityGuid entity) =>
        new(entity, W.Fields.Light.Intensity.Component, W.Fields.Light.Intensity.Id);

    public static void Run(TestContext t)
    {
        using var server = new FixtureServer(t);
        Hello(t, server);
        Framing(t, server);
        using (RemoteSession session = server.Attach())
        {
            Queries(t, session);
            Commands(t, session);
            Subscriptions(t, session);
            Control(t, session);
            Pipelining(t, session);
        }
        Failure(t);
    }

    private static void Hello(TestContext t, FixtureServer server)
    {
        using RemoteSession session = server.Attach();
        t.Equal(server.Session.Scene, session.Scene, "hello: the scene is the session file's");
        string schema = Json.Write(Codec.EncodeSchema(session.Schema));
        string expected = File.ReadAllLines(t.RepoPath("tests", "csharp", "conformance", "codec_vectors.jsonl"))
            .Select(Json.Parse).First(line => line.Find("case")!.AsString == "schema.world").Find("wire")!.Pipe(Json.Write);
        t.Check(schema == expected, "connection.schema: the World schema, as the C++ codec encodes it");
        t.Equal(3UL, session.Send("connection.findEntity", JsonValue.NewObject().Set("entity", Codec.Encode(Sun))),
                "request ids count from 1 (hello 1, schema 2, then 3)");

        RemoteException? bad = t.Throws<RemoteException>(
            () => RemoteSession.Connect(server.Session with { Token = new string('0', 32) }, FixtureServer.Options),
            "a wrong token is refused");
        t.Equal(RemoteError.BadToken, bad?.Error, "... as BadToken");

        using (var raw = new RawClient(server.Session.Port))
        {
            raw.Send("{\"id\":1,\"method\":\"hello\",\"params\":{\"protocol\":\"atlantis.remote/2\",\"token\":\"" +
                     server.Session.Token + "\"}}");
            t.Check(raw.Receive()?.Contains("\"code\":\"WrongProtocol\"") == true, "another protocol is refused: WrongProtocol");
            t.Check(raw.Receive() is null, "... and the connection closes");
        }
        using (var raw = new RawClient(server.Session.Port))
        {
            raw.Send("{\"id\":7,\"method\":\"connection.listEntities\"}");
            t.Equal("{\"id\":7,\"error\":{\"code\":\"HelloRequired\",\"message\":\"the first request must be hello\"}}",
                    raw.Receive(), "a request before hello: HelloRequired");
            t.Check(raw.Receive() is null, "... and the connection closes");
        }
    }

    private static void Framing(TestContext t, FixtureServer server)
    {
        using (var raw = new RawClient(server.Session.Port))
        {
            raw.Send("{\"id\":1,\"method\":\"hello\",\"params\":{\"protocol\":\"atlantis.remote/1\",\"token\":\"" +
                     server.Session.Token + "\"}}\r");
            t.Check(raw.Receive()?.StartsWith("{\"id\":1,\"result\":") == true, "a CRLF-terminated request is one line");
            raw.Send("not json");
            t.Equal("{\"id\":null,\"error\":{\"code\":\"BadRequest\",\"message\":\"a request is one JSON object per line\"}}",
                    raw.Receive(), "a line that is not a JSON object: BadRequest with a null id");
            raw.Send("{\"id\":\"x\",\"method\":\"connection.teleport\"}");
            t.Check(raw.Receive()?.StartsWith("{\"id\":\"x\",\"error\":{\"code\":\"BadRequest\"") == true,
                    "an unknown method: BadRequest, the id echoed unchanged");
            raw.Send("{\"id\":2,\"method\":\"hello\",\"params\":{}}");
            t.Check(raw.Receive()?.Contains("\"code\":\"BadRequest\"") == true, "a second hello: BadRequest");
            raw.Send("{\"id\":3,\"method\":\"connection.findEntity\",\"params\":{\"entity\":\"" + Sun + "\"}}");
            t.Equal("{\"id\":3,\"result\":true}", raw.Receive(), "the connection still answers");
            raw.Send("{\"id\":4,\"method\":\"connection.findEntity\",\"params\":{\"entity\":\"" + new string('a', 1024 * 1024) + "\"}}");
            t.Check(raw.Receive() is null, "a request line over 1 MiB disconnects the client");
        }
        using (var raw = new RawClient(server.Session.Port))
        {
            raw.Send("{\"id\":1,\"method\":\"hello\",\"params\":{\"protocol\":\"atlantis.remote/1\",\"token\":\"" +
                     server.Session.Token + "\"}}");
            string padded = "{\"id\":2,\"method\":\"connection.listEntities\",\"params\":{},\"pad\":\"";
            padded += new string('p', 1024 * 1024 - padded.Length - 2) + "\"}";
            t.Equal(1024 * 1024, Encoding.UTF8.GetByteCount(padded), "(a request of exactly 1 MiB)");
            raw.Send(padded);
            t.Check(raw.Receive()?.StartsWith("{\"id\":1,\"result\":") == true && raw.Receive()?.StartsWith("{\"id\":2,\"result\":[") == true,
                    "a request line of exactly 1 MiB is answered");
        }
    }

    private static void Queries(TestContext t, RemoteSession session)
    {
        t.Check(session.FindEntity(Sun), "findEntity: an entity of the scene");
        t.Check(!session.FindEntity(Unknown), "findEntity: an unknown GUID");
        t.Equal(string.Join(",", new[] { Mesh, Sun, Lamp, Camera, Empty }), string.Join(",", session.ListEntities()),
                "listEntities: the five, sorted by GUID");
        Outcome<IReadOnlyList<TypeId>, AccessError> components = session.ListComponents(Sun);
        t.Check(components.IsOk && components.Value.Contains(W.Fields.Light.Intensity.Component), "listComponents: the sun has a Light");
        t.Check(components.IsOk && components.Value.SequenceEqual(components.Value.OrderBy(id => id.Value)), "... sorted by TypeId");
        t.Equal(AccessError.UnknownEntity, session.ListComponents(Unknown).Error, "listComponents of an unknown entity: UnknownEntity");
        t.Equal(PropertyValue.FromFloat32(1.2f), session.GetProperty(Intensity(Sun)).Value, "getProperty: the sun's intensity 1.2");
        t.Equal(AccessError.ComponentMissing, session.GetProperty(Intensity(Empty)).Error, "getProperty without the component");
        t.Equal(AccessError.UnknownField,
                session.GetProperty(new PropertyAddress(Sun, W.Fields.Light.Intensity.Component, new FieldId(42))).Error,
                "getProperty of an unknown field");
        IReadOnlyList<Outcome<PropertyValue, AccessError>> many = session.GetProperties(new[]
        {
            Intensity(Sun), Intensity(Lamp), Intensity(Empty),
        });
        t.Check(many.Count == 3 && many[0].Value == PropertyValue.FromFloat32(1.2f) && many[1].Value == PropertyValue.FromFloat32(3.0f) &&
                many[2].Error == AccessError.ComponentMissing, "getProperties: pipelined, answers in input order");
        IReadOnlyList<Outcome<IReadOnlyList<TypeId>, AccessError>> lists = session.ListComponents(new[] { Sun, Unknown, Empty });
        t.Check(lists.Count == 3 && lists[0].IsOk && lists[1].Error == AccessError.UnknownEntity && lists[2].IsOk,
                "listComponents: pipelined, answers in input order");
    }

    // Waits for a frame boundary by asking a question (answered at the next one).
    public static void Settle(RemoteSession session, int boundaries = 2)
    {
        for (int i = 0; i < boundaries; ++i) session.FindEntity(Sun);
    }

    private static void Commands(TestContext t, RemoteSession session)
    {
        CommandTicket set = session.Submit(new SetProperty(Intensity(Sun), 2.5f));
        t.Check(set.Value > 0, "submit: a ticket");
        Settle(session);
        t.Equal(PropertyValue.FromFloat32(2.5f), session.GetProperty(Intensity(Sun)).Value, "the command applied at the next frame");

        CommandTicket nan = session.Submit(new SetProperty(Intensity(Sun), float.NaN));
        Settle(session);
        t.Check(session.DrainFailures().Contains(new CommandFailure(nan, AccessError.NonFiniteValue)),
                "a NaN is the World's refusal, by ticket: NonFiniteValue");
        t.Equal(0, session.DrainFailures().Count, "failures drain once");

        EntityGuid spawned = EntityGuid.Parse("58005800-0000-4000-8000-0000000000c1");
        TransactionTicket transaction = session.SubmitTransaction(new Command[]
        {
            new CreateEntity(spawned), new AddComponent(spawned, W.Fields.Light.Intensity.Component),
        });
        t.Equal(2UL, transaction.Count, "submitTransaction: one ticket per command");
        Settle(session);
        t.Check(session.FindEntity(spawned) && session.ListComponents(spawned).Value.Contains(W.Fields.Light.Intensity.Component),
                "the transaction applied whole");
        TransactionTicket refused = session.SubmitTransaction(new Command[]
        {
            new SetProperty(Intensity(spawned), 9.0f), new AddComponent(spawned, W.Fields.Light.Intensity.Component),
        });
        Settle(session);
        IReadOnlyList<CommandFailure> failures = session.DrainFailures();
        t.Check(failures.Count == 1 && refused.Contains(failures[0].Ticket) && failures[0].Error == AccessError.ComponentAlreadyPresent,
                "a refused transaction: one failure, its ticket in the range");
        t.Check(session.GetProperty(Intensity(spawned)).Value != PropertyValue.FromFloat32(9.0f), "... and none of it applied");
        t.Equal(0UL, session.SubmitTransaction(Array.Empty<Command>()).Count, "an empty transaction: count 0");
        session.Submit(new DestroyEntity(spawned));
        Settle(session);
        t.Check(!session.FindEntity(spawned), "destroy applied");
    }

    private static void Subscriptions(TestContext t, RemoteSession session)
    {
        SubscriptionId subscription = session.Subscribe(new EventFilter(EventKind.PropertyChanged, Sun));
        session.Submit(new SetProperty(Intensity(Sun), 4.0f));
        session.Submit(new SetProperty(Intensity(Lamp), 5.0f));
        Settle(session);
        Outcome<IReadOnlyList<Event>, ConnectionError> events = session.DrainEvents(subscription);
        t.Check(events.IsOk && events.Value.Count == 1 &&
                events.Value[0] == new PropertyChanged(Intensity(Sun), PropertyValue.FromFloat32(4.0f)),
                "drainEvents: the filter's events only, with the applied value");
        t.Equal(0, session.DrainEvents(subscription).Value.Count, "events drain once");
        t.Equal(null, session.Unsubscribe(subscription), "unsubscribe");
        t.Equal(ConnectionError.UnknownSubscription, session.Unsubscribe(subscription), "unsubscribe twice: UnknownSubscription");
        t.Equal(ConnectionError.UnknownSubscription, session.DrainEvents(subscription).Error, "drainEvents after: UnknownSubscription");
    }

    private static void Control(TestContext t, RemoteSession session)
    {
        RemoteControl control = session.Control;
        RuntimeStatus before = control.Status();
        t.Check(!before.Paused && before.Scene == session.Scene, "status: running, the session's scene");
        control.Pause();
        t.Check(control.Status().Paused, "pause");
        session.Submit(new SetProperty(Intensity(Sun), 6.0f));
        Settle(session, 4);
        t.Equal(PropertyValue.FromFloat32(4.0f), session.GetProperty(Intensity(Sun)).Value, "paused: commands are held");
        StepResult step = control.Step(new StepRequest(1));
        t.Check(step.IsOk && step.Report!.Applied && step.Report.Frame > before.Frame, "step(1): a report after its frame");
        t.Equal(PropertyValue.FromFloat32(6.0f), session.GetProperty(Intensity(Sun)).Value, "the step applied the held command");
        t.Equal(ControlError.InvalidRequest, control.Step(new StepRequest(0)).Error, "step(0): InvalidRequest");
        t.Equal(ControlError.NotRendering, control.Step(new StepRequest(1, "frame.png")).Error,
                "an image from a server without a renderer: NotRendering");
        ulong pending = control.SendStep(new StepRequest(3));
        t.Check(session.FindEntity(Sun), "a parked step does not block other requests");
        t.Check(control.AwaitStep(pending).IsOk, "... and is answered after its frames");
        control.Resume();
        t.Check(!control.Status().Paused, "resume");
        DiagnosticBatch diagnostics = control.Diagnostics(0, 16);
        t.Check(diagnostics.Entries.Count == 0 && diagnostics.Latest == 0, "diagnostics: none");
        session.Submit(new SetProperty(Intensity(Sun), 1.2f));
        Settle(session);
    }

    private static void Pipelining(TestContext t, RemoteSession session)
    {
        var ids = new List<(ulong Id, EntityGuid Entity)>();
        foreach (EntityGuid entity in new[] { Sun, Unknown, Lamp, Unknown, Empty, Mesh })
        {
            ids.Add((session.Send("connection.findEntity", JsonValue.NewObject().Set("entity", Codec.Encode(entity))), entity));
        }
        bool all = true;
        for (int i = ids.Count - 1; i >= 0; --i)
        {
            all &= session.AwaitResult(ids[i].Id).AsBool == (ids[i].Entity != Unknown);
        }
        t.Check(all, "pipelined requests awaited in reverse order: each answer by its id");
    }

    private static void Failure(TestContext t)
    {
        var server = new FixtureServer(t);
        RemoteSession session = server.Attach();
        t.Equal(0, server.Stop(), "the fixture server exits 0 when its standard input closes");
        t.Check(!File.Exists(server.SessionPath), "... and removes its session file");
        RemoteException? first = t.Throws<RemoteException>(() => session.ListEntities(), "a call after the server is gone throws");
        t.Equal(RemoteError.Disconnected, first?.Error, "... Disconnected");
        t.Check(ReferenceEquals(first, t.Throws<RemoteException>(() => session.FindEntity(Sun), "a later call throws again")),
                "... the same first failure");
        t.Equal(RemoteError.Disconnected, session.Failure, "Failure names it");
        session.Dispose();
        server.Dispose();
        t.Equal(RemoteError.ConnectFailed,
                t.Throws<RemoteException>(() => RemoteSession.Connect(server.Session, FixtureServer.Options), "nothing listening")?.Error,
                "ConnectFailed");
    }

    private static TResult Pipe<T, TResult>(this T value, Func<T, TResult> f) => f(value);
}

/// <summary>A bare line client, for the protocol's own refusals.</summary>
public sealed class RawClient : IDisposable
{
    private readonly TcpClient _client;
    private readonly NetworkStream _stream;
    private readonly List<byte> _buffer = new();

    public RawClient(int port)
    {
        _client = new TcpClient();
        _client.Connect(IPAddress.Loopback, port);
        _stream = _client.GetStream();
        _stream.ReadTimeout = 20000;
    }

    public void Send(string line)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(line + "\n");
        try
        {
            _stream.Write(bytes, 0, bytes.Length);
        }
        catch (IOException)
        {
            // the server may already have closed
        }
    }

    /// <summary>The next line, or null once the server has closed the connection.</summary>
    public string? Receive()
    {
        var chunk = new byte[65536];
        while (true)
        {
            int newline = _buffer.IndexOf((byte)'\n');
            if (newline >= 0)
            {
                string line = Encoding.UTF8.GetString(_buffer.GetRange(0, newline).ToArray());
                _buffer.RemoveRange(0, newline + 1);
                return line;
            }
            int read;
            try
            {
                read = _stream.Read(chunk, 0, chunk.Length);
            }
            catch (IOException)
            {
                return null;
            }
            if (read == 0) return null;
            _buffer.AddRange(chunk.AsSpan(0, read).ToArray());
        }
    }

    public void Dispose() => _client.Dispose();
}
