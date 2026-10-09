// Spec 0058 R1, ADR-0112 D1, Plan 0058 P6: one attached session over
// atlantis.remote/1 -- a loopback TCP socket, the hello, JSON-lines framing,
// request ids from 1 and pipelining (send several, then await each), with
// the C++ client half's limits and timeouts (src/remote/src/remote_client.cpp).
// Each connection.* method is one request whose result decodes into the
// SDK's value types; the schema is fetched once at connect.
//
// Failures are thrown as RemoteException. After the first one the session is
// failed: every later call throws again with that first failure.
//
// Not thread-safe: one thread uses a session. Each call blocks until its
// answer arrives, at the Runtime's next frame boundary.
using System;
using System.Collections.Generic;
using System.Net;
using System.Net.Sockets;
using System.Text;

namespace Atlantis.Gameplay.Remote;

public sealed class RemoteOptions
{
    public int ConnectTimeoutMilliseconds { get; init; } = 5000;

    /// <summary>
    /// How long one response may take. A response comes at the Runtime's next
    /// frame boundary, but a first frame that realizes a large scene's
    /// materials can take long.
    /// </summary>
    public int ResponseTimeoutMilliseconds { get; init; } = 120000;
}

public sealed class RemoteSession : IDisposable
{
    private const int MaxResponseLineBytes = 256 * 1024 * 1024;  // a large scene's listEntities
    private const int ChunkBytes = 64 * 1024;

    private readonly Socket _socket;
    private readonly RemoteOptions _options;
    private readonly Dictionary<ulong, JsonValue> _responses = new();  // by id: arrived, not yet taken
    private readonly List<byte> _partial = new();
    private readonly byte[] _chunk = new byte[ChunkBytes];
    private ulong _nextId = 1;
    private RemoteException? _failure;
    private IReadOnlyList<SchemaType> _schema = Array.Empty<SchemaType>();

    private RemoteSession(Socket socket, RemoteOptions options)
    {
        _socket = socket;
        _options = options;
        Control = new RemoteControl(this);
    }

    /// <summary>Connects to the session's port, says hello with its token, and fetches the schema.</summary>
    public static RemoteSession Connect(SessionInfo session, RemoteOptions? options = null)
    {
        options ??= new RemoteOptions();
        var socket = new Socket(AddressFamily.InterNetwork, SocketType.Stream, ProtocolType.Tcp) { NoDelay = true };
        try
        {
            if (!socket.ConnectAsync(new IPEndPoint(IPAddress.Loopback, session.Port)).Wait(options.ConnectTimeoutMilliseconds))
            {
                throw new SocketException((int)SocketError.TimedOut);
            }
        }
        catch (Exception e) when (e is SocketException or AggregateException)
        {
            socket.Dispose();
            throw new RemoteException(RemoteError.ConnectFailed, "127.0.0.1:" + session.Port);
        }

        var remote = new RemoteSession(socket, options);
        try
        {
            remote.Hello(session.Token);
            JsonValue schema = remote.Call("connection.schema");
            remote._schema = Codec.DecodeSchema(schema) ?? throw remote.Fail(RemoteError.ProtocolError, "a malformed schema");
            return remote;
        }
        catch
        {
            remote.Dispose();
            throw;
        }
    }

    private void Hello(string token)
    {
        JsonValue hello = JsonValue.NewObject().Set("protocol", JsonValue.String(SessionFile.Protocol))
            .Set("token", JsonValue.String(token));
        JsonValue response = AwaitResponse(Send("hello", hello));
        if (response.Find("error") is JsonValue error)
        {
            string code = error.Find("code") is { IsString: true } c ? c.AsString : "";
            throw Fail(code switch
            {
                "BadToken" => RemoteError.BadToken,
                "WrongProtocol" => RemoteError.WrongProtocol,
                _ => RemoteError.ProtocolError,
            }, "hello refused: " + code);
        }
        if (response.Find("result") is not JsonValue result || result.Find("scene") is not JsonValue scene ||
            Codec.DecodeAsset(scene) is not AssetGuid sceneGuid)
        {
            throw Fail(RemoteError.ProtocolError, "a malformed hello result");
        }
        Scene = sceneGuid;
    }

    /// <summary>The Runtime's scene, from the hello.</summary>
    public AssetGuid Scene { get; private set; }

    /// <summary>The Runtime's schema, fetched once at connect.</summary>
    public IReadOnlyList<SchemaType> Schema => _schema;

    /// <summary>control.* over this session.</summary>
    public RemoteControl Control { get; }

    /// <summary>The first failure, once the session has failed.</summary>
    public RemoteError? Failure => _failure?.Error;

    public void Dispose()
    {
        try
        {
            _socket.Shutdown(SocketShutdown.Both);
        }
        catch (SocketException)
        {
        }
        catch (ObjectDisposedException)
        {
        }
        _socket.Dispose();
    }

    // --- Raw requests: the protocol itself (remote_protocol.md section 4).

    /// <summary>Sends one request without waiting; its id (from 1).</summary>
    public ulong Send(string method, JsonValue? parameters = null)
    {
        ThrowIfFailed();
        ulong id = _nextId++;
        JsonValue request = JsonValue.NewObject().Set("id", JsonValue.Number(id)).Set("method", JsonValue.String(method))
            .Set("params", parameters ?? JsonValue.NewObject());
        SendLine(Json.Write(request));
        return id;
    }

    /// <summary>Sends one raw line (a newline is appended). For protocol tests.</summary>
    public void SendLine(string line)
    {
        ThrowIfFailed();
        byte[] bytes = Encoding.UTF8.GetBytes(line + "\n");
        try
        {
            int sent = 0;
            while (sent < bytes.Length) sent += _socket.Send(bytes, sent, bytes.Length - sent, SocketFlags.None);
        }
        catch (Exception e) when (e is SocketException or ObjectDisposedException)
        {
            throw Fail(RemoteError.Disconnected, "sending: " + e.Message);
        }
    }

    /// <summary>The whole response envelope for <paramref name="id"/>, once it arrives.</summary>
    public JsonValue AwaitResponse(ulong id)
    {
        ThrowIfFailed();
        long deadline = Environment.TickCount64 + _options.ResponseTimeoutMilliseconds;
        while (true)
        {
            if (_responses.Remove(id, out JsonValue? response)) return response;
            long remaining = deadline - Environment.TickCount64;
            if (remaining <= 0) throw Fail(RemoteError.Timeout, "no response to request " + id);
            Receive((int)Math.Min(remaining, 50));
        }
    }

    /// <summary>The result of <paramref name="id"/>; an error response fails the session (ProtocolError).</summary>
    public JsonValue AwaitResult(ulong id)
    {
        JsonValue response = AwaitResponse(id);
        if (response.Find("result") is JsonValue result) return result;
        string detail = response.Find("error") is JsonValue error ? Json.Write(error) : "no result";
        throw Fail(RemoteError.ProtocolError, detail);
    }

    public JsonValue Call(string method, JsonValue? parameters = null) => AwaitResult(Send(method, parameters));

    // Takes in every complete response line available within the wait.
    private void Receive(int waitMilliseconds)
    {
        int received;
        try
        {
            if (!_socket.Poll(waitMilliseconds * 1000, SelectMode.SelectRead)) return;
            received = _socket.Receive(_chunk);
        }
        catch (Exception e) when (e is SocketException or ObjectDisposedException)
        {
            throw Fail(RemoteError.Disconnected, e.Message);
        }
        if (received == 0) throw Fail(RemoteError.Disconnected, "the Runtime closed the connection");
        int start = 0;
        for (int i = 0; i < received; ++i)
        {
            if (_chunk[i] != (byte)'\n') continue;
            AppendPartial(start, i - start);
            int length = _partial.Count;
            if (length > 0 && _partial[length - 1] == (byte)'\r') --length;
            TakeLine(_partial.GetRange(0, length).ToArray());
            _partial.Clear();
            start = i + 1;
        }
        AppendPartial(start, received - start);
    }

    private void AppendPartial(int start, int count)
    {
        for (int i = 0; i < count; ++i) _partial.Add(_chunk[start + i]);
        if (_partial.Count > MaxResponseLineBytes) throw Fail(RemoteError.ProtocolError, "a response line over the limit");
    }

    private void TakeLine(byte[] line)
    {
        JsonValue parsed;
        try
        {
            parsed = Json.Parse(line);
        }
        catch (FormatException e)
        {
            throw Fail(RemoteError.ProtocolError, e.Message);
        }
        if (!parsed.IsObject || parsed.Find("id") is not JsonValue id || !id.TryGetUInt64(out ulong key))
        {
            throw Fail(RemoteError.ProtocolError, "a response without an integer id");
        }
        _responses[key] = parsed;
    }

    private void ThrowIfFailed()
    {
        if (_failure is not null) throw _failure;
    }

    // Records the first failure; returns the exception to throw (always the first).
    internal RemoteException Fail(RemoteError error, string detail)
    {
        _failure ??= new RemoteException(error, detail);
        return _failure;
    }

    internal RemoteException Malformed(string method) => Fail(RemoteError.ProtocolError, "a malformed " + method + " result");

    // Decodes an in-band {"ok":...}/{"err":"..."} result.
    internal Outcome<T, TError> InBand<T, TError>(JsonValue result, string method, Func<JsonValue, T?> decodeOk,
                                                  Func<JsonValue, TError?> decodeErr)
        where TError : struct, Enum
    {
        if (result.Find("ok") is JsonValue ok)
        {
            T? value = decodeOk(ok);
            return value is not null ? Outcome<T, TError>.Ok(value) : throw Malformed(method);
        }
        if (result.Find("err") is JsonValue err)
        {
            return decodeErr(err) is TError error ? Outcome<T, TError>.Err(error) : throw Malformed(method);
        }
        throw Malformed(method);
    }

    // --- connection.* (remote_protocol.md section 5).

    private static JsonValue EntityParams(EntityGuid entity) => JsonValue.NewObject().Set("entity", Codec.Encode(entity));

    private static JsonValue AddressParams(PropertyAddress address) =>
        JsonValue.NewObject().Set("address", Codec.Encode(address));

    private static JsonValue SubscriptionParams(SubscriptionId subscription) =>
        JsonValue.NewObject().Set("subscription", JsonValue.Number(subscription.Value));

    public bool FindEntity(EntityGuid entity)
    {
        JsonValue result = Call("connection.findEntity", EntityParams(entity));
        return result.IsBool ? result.AsBool : throw Malformed("connection.findEntity");
    }

    /// <summary>Every entity, sorted by GUID.</summary>
    public IReadOnlyList<EntityGuid> ListEntities()
    {
        JsonValue result = Call("connection.listEntities");
        if (!result.IsArray) throw Malformed("connection.listEntities");
        var entities = new List<EntityGuid>(result.Items.Count);
        foreach (JsonValue item in result.Items)
        {
            entities.Add(Codec.DecodeEntity(item) ?? throw Malformed("connection.listEntities"));
        }
        return entities;
    }

    public Outcome<IReadOnlyList<TypeId>, AccessError> ListComponents(EntityGuid entity) =>
        DecodeComponents(AwaitResult(Send("connection.listComponents", EntityParams(entity))));

    public Outcome<PropertyValue, AccessError> GetProperty(PropertyAddress address) =>
        DecodeProperty(AwaitResult(Send("connection.getProperty", AddressParams(address))));

    /// <summary>Pipelined: every request is sent before any answer is awaited; answers in input order.</summary>
    public IReadOnlyList<Outcome<IReadOnlyList<TypeId>, AccessError>> ListComponents(IReadOnlyList<EntityGuid> entities)
    {
        var ids = new List<ulong>(entities.Count);
        foreach (EntityGuid entity in entities) ids.Add(Send("connection.listComponents", EntityParams(entity)));
        var answers = new List<Outcome<IReadOnlyList<TypeId>, AccessError>>(ids.Count);
        foreach (ulong id in ids) answers.Add(DecodeComponents(AwaitResult(id)));
        return answers;
    }

    /// <summary>Pipelined: every request is sent before any answer is awaited; answers in input order.</summary>
    public IReadOnlyList<Outcome<PropertyValue, AccessError>> GetProperties(IReadOnlyList<PropertyAddress> addresses)
    {
        var ids = new List<ulong>(addresses.Count);
        foreach (PropertyAddress address in addresses) ids.Add(Send("connection.getProperty", AddressParams(address)));
        var answers = new List<Outcome<PropertyValue, AccessError>>(ids.Count);
        foreach (ulong id in ids) answers.Add(DecodeProperty(AwaitResult(id)));
        return answers;
    }

    private Outcome<IReadOnlyList<TypeId>, AccessError> DecodeComponents(JsonValue result) =>
        InBand<IReadOnlyList<TypeId>, AccessError>(result, "connection.listComponents", ok =>
        {
            if (!ok.IsArray) return null;
            var types = new List<TypeId>(ok.Items.Count);
            foreach (JsonValue item in ok.Items)
            {
                if (Codec.DecodeTypeId(item) is not TypeId type) return null;
                types.Add(type);
            }
            return types;
        }, Codec.DecodeAccessError);

    private Outcome<PropertyValue, AccessError> DecodeProperty(JsonValue result)
    {
        Outcome<PropertyValue?, AccessError> decoded =
            InBand<PropertyValue?, AccessError>(result, "connection.getProperty", Codec.DecodePropertyValue, Codec.DecodeAccessError);
        return decoded.IsOk ? Outcome<PropertyValue, AccessError>.Ok(decoded.Value!.Value)
                            : Outcome<PropertyValue, AccessError>.Err(decoded.Error);
    }

    public CommandTicket Submit(Command command)
    {
        JsonValue result = Call("connection.submit", JsonValue.NewObject().Set("command", Codec.Encode(command)));
        return Codec.DecodeUInt64(result) is ulong ticket ? new CommandTicket(ticket) : throw Malformed("connection.submit");
    }

    public TransactionTicket SubmitTransaction(IReadOnlyList<Command> commands)
    {
        JsonValue list = JsonValue.NewArray();
        foreach (Command command in commands) list.Push(Codec.Encode(command));
        JsonValue result = Call("connection.submitTransaction", JsonValue.NewObject().Set("commands", list));
        return Codec.DecodeTransactionTicket(result) ?? throw Malformed("connection.submitTransaction");
    }

    public SubscriptionId Subscribe(EventFilter filter)
    {
        JsonValue result = Call("connection.subscribe", JsonValue.NewObject().Set("filter", Codec.Encode(filter)));
        return Codec.DecodeUInt64(result) is ulong id ? new SubscriptionId(id) : throw Malformed("connection.subscribe");
    }

    /// <summary>Null when released; UnknownSubscription otherwise.</summary>
    public ConnectionError? Unsubscribe(SubscriptionId subscription)
    {
        JsonValue result = Call("connection.unsubscribe", SubscriptionParams(subscription));
        if (result.Find("ok") is JsonValue ok) return ok.IsNull ? null : throw Malformed("connection.unsubscribe");
        if (result.Find("err") is JsonValue err)
        {
            return Codec.DecodeConnectionError(err) ?? throw Malformed("connection.unsubscribe");
        }
        throw Malformed("connection.unsubscribe");
    }

    /// <summary>The matching events since the last drain, in application order.</summary>
    public Outcome<IReadOnlyList<Event>, ConnectionError> DrainEvents(SubscriptionId subscription)
    {
        JsonValue result = Call("connection.drainEvents", SubscriptionParams(subscription));
        return InBand<IReadOnlyList<Event>, ConnectionError>(result, "connection.drainEvents", ok =>
        {
            if (!ok.IsArray) return null;
            var events = new List<Event>(ok.Items.Count);
            foreach (JsonValue item in ok.Items)
            {
                if (Codec.DecodeEvent(item) is not Event evt) return null;
                events.Add(evt);
            }
            return events;
        }, Codec.DecodeConnectionError);
    }

    /// <summary>Failures of this connection's commands since the last drain.</summary>
    public IReadOnlyList<CommandFailure> DrainFailures()
    {
        JsonValue result = Call("connection.drainFailures");
        if (!result.IsArray) throw Malformed("connection.drainFailures");
        var failures = new List<CommandFailure>(result.Items.Count);
        foreach (JsonValue item in result.Items)
        {
            failures.Add(Codec.DecodeFailure(item) ?? throw Malformed("connection.drainFailures"));
        }
        return failures;
    }
}

/// <summary>
/// control.* over a session (ADR-0106 D3). A step is answered after its last
/// frame; <see cref="SendStep"/> and <see cref="AwaitStep"/> let a client do
/// other work meanwhile. Not thread-safe (the session's thread).
/// </summary>
public sealed class RemoteControl
{
    private readonly RemoteSession _session;

    internal RemoteControl(RemoteSession session) => _session = session;

    public RuntimeStatus Status() =>
        Codec.DecodeStatus(_session.Call("control.status")) ?? throw _session.Malformed("control.status");

    public void Pause() => _session.Call("control.pause");

    public void Resume() => _session.Call("control.resume");

    /// <summary>Runs the request's frames and waits for its report.</summary>
    public StepResult Step(StepRequest request) => AwaitStep(SendStep(request));

    /// <summary>Asks for a step without waiting; pass the id to <see cref="AwaitStep"/>.</summary>
    public ulong SendStep(StepRequest request) =>
        _session.Send("control.step", JsonValue.NewObject().Set("request", Codec.Encode(request)));

    public StepResult AwaitStep(ulong id)
    {
        Outcome<FrameReport, ControlError> outcome = _session.InBand<FrameReport, ControlError>(
            _session.AwaitResult(id), "control.step", Codec.DecodeFrameReport, Codec.DecodeControlError);
        return outcome.IsOk ? new StepResult(outcome.Value, null) : new StepResult(null, outcome.Error);
    }

    /// <summary>Diagnostics with a sequence after <paramref name="afterSequence"/>, at most <paramref name="max"/>.</summary>
    public DiagnosticBatch Diagnostics(ulong afterSequence, ulong max) =>
        Codec.DecodeDiagnosticBatch(_session.Call("control.diagnostics",
            JsonValue.NewObject().Set("after", JsonValue.Number(afterSequence)).Set("max", JsonValue.Number(max))))
        ?? throw _session.Malformed("control.diagnostics");
}
