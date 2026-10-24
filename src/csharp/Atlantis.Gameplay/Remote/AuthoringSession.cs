// Plan 0065 P6 (Spec 0065 R11, Q8 G-b; ADR-0130 D1, D3): authoring.* over a
// session -- the C# counterpart of the C++ RemoteAuthoring
// (src/remote/src/remote_client.cpp), one request per call. Reflective only:
// a scene component and its fields are named, as the C# SDK's World paths are
// (SchemaText), against the authoring schema -- fetched once, at the first
// call that needs it -- and values are PropertyValue. No typed bindings of the
// scene components (atlantis_sdk_codegen is not involved).
//
// Refusals come back in band: a query's as the Outcome's error, a command's
// later as a CommandFailure under its ticket, Open/Apply/Save's as their error
// (null when they succeeded). A Runtime that serves no authoring session
// answers NoAuthoring, which fails the session (RemoteError.ProtocolError,
// "NoAuthoring" in its detail), as NoControl does for control.*.
//
// Apply returns once the Runtime has accepted the document; the reload itself
// happens at the Runtime's next frame that applies commands -- while it is
// paused, at the next step or on resume (Spec 0065 third Correction) -- and is
// reported to every subscription as Applied{generation}.
//
// Not thread-safe: the session's thread.
using System;
using System.Collections.Generic;
using A = Atlantis.Gameplay.Authoring;

namespace Atlantis.Gameplay.Remote;

public sealed class AuthoringSession
{
    private readonly RemoteSession _session;
    private IReadOnlyList<SchemaType>? _schema;

    internal AuthoringSession(RemoteSession session) => _session = session;

    // --- The document.

    /// <summary>(Re)reads the Runtime's scene from its source; null on success.</summary>
    public A.OpenError? Open() => Unit("authoring.open", Codec.DecodeOpenError);

    /// <summary>The open document's identity; null before a successful Open.</summary>
    public A.DocumentInfo? Document()
    {
        JsonValue result = _session.Call("authoring.document");
        if (result.IsNull) return null;
        return Codec.DecodeDocumentInfo(result) ?? throw _session.Malformed("authoring.document");
    }

    /// <summary>Hands the document to the Runtime for reload; null when accepted (Applied follows).</summary>
    public A.ApplyError? Apply() => Unit("authoring.apply", Codec.DecodeApplyError);

    /// <summary>Writes a hand-authored document to its source; null on success.</summary>
    public A.SaveError? Save() => Unit("authoring.save", Codec.DecodeSaveError);

    // --- Queries.

    /// <summary>Every node, in the document's order.</summary>
    public Outcome<IReadOnlyList<A.NodeSummary>, A.AuthoringError> ListNodes() =>
        _session.InBand<IReadOnlyList<A.NodeSummary>, A.AuthoringError>(_session.Call("authoring.listNodes"), "authoring.listNodes",
            ok => Values(ok, Codec.DecodeNodeSummary), Codec.DecodeAuthoringError);

    /// <summary>A node's component types, sorted.</summary>
    public Outcome<IReadOnlyList<TypeId>, A.AuthoringError> ListComponents(EntityGuid node) =>
        _session.InBand<IReadOnlyList<TypeId>, A.AuthoringError>(
            _session.Call("authoring.listComponents", JsonValue.NewObject().Set("node", Codec.Encode(node))),
            "authoring.listComponents", ok => Values(ok, Codec.DecodeTypeId), Codec.DecodeAuthoringError);

    public Outcome<PropertyValue, A.AuthoringError> GetProperty(A.SceneAddress address)
    {
        Outcome<PropertyValue?, A.AuthoringError> decoded = _session.InBand<PropertyValue?, A.AuthoringError>(
            _session.Call("authoring.getProperty", JsonValue.NewObject().Set("address", Codec.Encode(address))),
            "authoring.getProperty", Codec.DecodePropertyValue, Codec.DecodeAuthoringError);
        return decoded.IsOk ? Outcome<PropertyValue, A.AuthoringError>.Ok(decoded.Value!.Value)
                            : Outcome<PropertyValue, A.AuthoringError>.Err(decoded.Error);
    }

    /// <summary>A property by path ("Light.intensity"); throws <see cref="UnknownNameException"/> if it does not resolve.</summary>
    public Outcome<PropertyValue, A.AuthoringError> GetProperty(EntityGuid node, string path) => GetProperty(Address(node, path));

    /// <summary>The active camera's node; null when the document has none.</summary>
    public Outcome<EntityGuid?, A.AuthoringError> ActiveCamera()
    {
        JsonValue result = _session.Call("authoring.activeCamera");
        if (result.Find("ok") is JsonValue ok)
        {
            if (ok.IsNull) return Outcome<EntityGuid?, A.AuthoringError>.Ok(null);
            return Codec.DecodeEntity(ok) is EntityGuid camera ? Outcome<EntityGuid?, A.AuthoringError>.Ok(camera)
                                                              : throw _session.Malformed("authoring.activeCamera");
        }
        if (result.Find("err") is JsonValue err && Codec.DecodeAuthoringError(err) is A.AuthoringError error)
        {
            return Outcome<EntityGuid?, A.AuthoringError>.Err(error);
        }
        throw _session.Malformed("authoring.activeCamera");
    }

    /// <summary>The scene's component types (section 6.7), fetched once.</summary>
    public IReadOnlyList<SchemaType> Schema =>
        _schema ??= Codec.DecodeSchema(_session.Call("authoring.schema")) ?? throw _session.Malformed("authoring.schema");

    // --- Names, through the authoring schema (Q8: reflective).

    /// <summary>A scene component type by short or qualified name ("Light"); throws <see cref="UnknownNameException"/>.</summary>
    public TypeId Type(string name)
    {
        SchemaType? type = SchemaText.FindType(Schema, name);
        return type is { Kind: TypeKind.Struct } ? type.Id : throw new UnknownNameException(NameError.UnknownType, name);
    }

    /// <summary>A node's property by path ("Light.intensity", "Camera.fog.density"); throws <see cref="UnknownNameException"/>.</summary>
    public A.SceneAddress Address(EntityGuid node, string path) =>
        SchemaText.TryParsePath(Schema, path, out TypeId component, out SchemaField? leaf, out NameError error)
            ? new A.SceneAddress(node, component, leaf!.Id)
            : throw new UnknownNameException(error, path);

    // --- Commands: applied when submitted, refusals reported by ticket.

    public CommandTicket Submit(A.Command command)
    {
        JsonValue result = _session.Call("authoring.submit", JsonValue.NewObject().Set("command", Codec.Encode(command)));
        return Codec.DecodeUInt64(result) is ulong ticket ? new CommandTicket(ticket) : throw _session.Malformed("authoring.submit");
    }

    /// <summary>All or nothing: every command applies, or none does and one failure names the transaction.</summary>
    public TransactionTicket SubmitTransaction(IReadOnlyList<A.Command> commands)
    {
        JsonValue list = JsonValue.NewArray();
        foreach (A.Command command in commands) list.Push(Codec.Encode(command));
        JsonValue result = _session.Call("authoring.submitTransaction", JsonValue.NewObject().Set("commands", list));
        return Codec.DecodeTransactionTicket(result) ?? throw _session.Malformed("authoring.submitTransaction");
    }

    /// <summary>Refusals of this session's submissions since the last drain.</summary>
    public IReadOnlyList<A.CommandFailure> DrainFailures()
    {
        JsonValue result = _session.Call("authoring.drainFailures");
        return Values(result, Codec.DecodeAuthoringFailure) ?? throw _session.Malformed("authoring.drainFailures");
    }

    // --- Events: every client's changes, and every Applied.

    public SubscriptionId Subscribe(A.EventFilter filter)
    {
        JsonValue result = _session.Call("authoring.subscribe", JsonValue.NewObject().Set("filter", Codec.Encode(filter)));
        return Codec.DecodeUInt64(result) is ulong id ? new SubscriptionId(id) : throw _session.Malformed("authoring.subscribe");
    }

    /// <summary>Null when released; UnknownSubscription otherwise.</summary>
    public ConnectionError? Unsubscribe(SubscriptionId subscription)
    {
        JsonValue result = _session.Call("authoring.unsubscribe", SubscriptionParams(subscription));
        if (result.Find("ok") is JsonValue ok) return ok.IsNull ? null : throw _session.Malformed("authoring.unsubscribe");
        if (result.Find("err") is JsonValue err)
        {
            return Codec.DecodeConnectionError(err) ?? throw _session.Malformed("authoring.unsubscribe");
        }
        throw _session.Malformed("authoring.unsubscribe");
    }

    /// <summary>The matching events since the last drain, in the order applied.</summary>
    public Outcome<IReadOnlyList<A.Event>, ConnectionError> DrainEvents(SubscriptionId subscription) =>
        _session.InBand<IReadOnlyList<A.Event>, ConnectionError>(
            _session.Call("authoring.drainEvents", SubscriptionParams(subscription)), "authoring.drainEvents",
            ok => Records(ok, Codec.DecodeAuthoringEvent), Codec.DecodeConnectionError);

    private static JsonValue SubscriptionParams(SubscriptionId subscription) =>
        JsonValue.NewObject().Set("subscription", JsonValue.Number(subscription.Value));

    // {"ok":null} -> null; {"err":"<name>"} -> the error.
    private TError? Unit<TError>(string method, Func<JsonValue, TError?> decodeErr) where TError : struct, Enum
    {
        JsonValue result = _session.Call(method);
        if (result.Find("ok") is JsonValue ok) return ok.IsNull ? null : throw _session.Malformed(method);
        if (result.Find("err") is JsonValue err) return decodeErr(err) ?? throw _session.Malformed(method);
        throw _session.Malformed(method);
    }

    private static List<T>? Values<T>(JsonValue value, Func<JsonValue, T?> decode) where T : struct
    {
        if (!value.IsArray) return null;
        var items = new List<T>(value.Items.Count);
        foreach (JsonValue item in value.Items)
        {
            if (decode(item) is not T decoded) return null;
            items.Add(decoded);
        }
        return items;
    }

    private static List<T>? Records<T>(JsonValue value, Func<JsonValue, T?> decode) where T : class
    {
        if (!value.IsArray) return null;
        var items = new List<T>(value.Items.Count);
        foreach (JsonValue item in value.Items)
        {
            if (decode(item) is not T decoded) return null;
            items.Add(decoded);
        }
        return items;
    }
}
