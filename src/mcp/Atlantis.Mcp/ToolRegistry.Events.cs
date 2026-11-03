using System;
using System.Collections.Immutable;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Remote;
using A = Atlantis.Gameplay.Authoring;

namespace Atlantis.Mcp;

internal sealed partial class ToolRegistry
{
    private JsonValue? _worldSubscription;
    private JsonValue? _authoringSubscription;

    private static ImmutableArray<ToolMapping> EventMappings { get; } =
    [
        Declare("world.drain_events", "connection.drainEvents",
            "Return and clear this host's World subscription: every event kind, no entity/component restriction. Does not drain failures.", WorldModel),
        Declare("authoring.drain_events", "authoring.drainEvents",
            "Return and clear this host's document subscription, including Applied{generation}. Does not drain failures.", AuthoringModel),
    ];

    /// <summary>
    /// Called exactly once after connecting, before serving tools. Not concurrent with tool calls.
    /// The wire owns the queues and releases subscriptions when this session disconnects. Retain a
    /// refused subscription's envelope so its drain reports the reason verbatim without retrying.
    /// </summary>
    internal void InitializeSubscriptions()
    {
        if (_worldSubscription is not null || _authoringSubscription is not null)
            throw new InvalidOperationException("Subscriptions already initialized");
        // Default SDK filters have all kinds and no predicates: an unrestricted filter, rather
        // than an empty kinds set (which would match nothing on the wire).
        _worldSubscription = Subscribe("connection.subscribe", Codec.Encode(new EventFilter()));
        _authoringSubscription = Subscribe("authoring.subscribe", Codec.Encode(new A.EventFilter()));
    }

    private JsonValue Subscribe(string method, JsonValue filter)
    {
        JsonValue response = session.Request(method, JsonValue.NewObject().Set("filter", filter));
        if (response.Find("error") is not null) return response;
        if (response.Find("result")?.TryGetUInt64(out _) != true)
            throw new RemoteException(RemoteError.ProtocolError, "malformed " + method + " result");
        return response;
    }

    private JsonValue SubscriptionFor(string method) =>
        (method == "connection.drainEvents" ? _worldSubscription : _authoringSubscription)
        ?? throw new InvalidOperationException("Subscriptions must be initialized before draining events");
}
