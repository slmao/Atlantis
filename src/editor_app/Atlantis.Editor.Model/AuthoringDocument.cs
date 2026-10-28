// Plan 0068 M2 (Spec 0068 R3, R7; ADR-0135): the editor's view of the
// authoring document -- the calls of the C# SDK's reflective AuthoringSession
// the models use (Spec 0065 R3-R7), behind an interface so the models run
// headless against a fake. RemoteAuthoringDocument is the real one: one
// session, one subscription to every authoring event. Authoring commands
// apply, or are refused, when submitted (ADR-0130 D1), so a commit settles
// as soon as its failures are drained. UI-free.
using System;
using System.Collections.Generic;
using System.Linq;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Authoring;
using Atlantis.Gameplay.Remote;

namespace Atlantis.Editor.Model;

public interface IAuthoringDocument
{
    /// <summary>The scene components' schema (section 6.7 of the protocol).</summary>
    IReadOnlyList<SchemaType> Schema { get; }

    /// <summary>The document's identity; null before it is open.</summary>
    DocumentInfo? Info();

    /// <summary>Every node in the document's order with its parent; empty without a document.</summary>
    IReadOnlyList<NodeSummary> ListNodes();

    /// <summary>A node's component types, sorted; empty for an unknown node.</summary>
    IReadOnlyList<TypeId> ListComponents(EntityGuid node);

    /// <summary>A property's value; null when it cannot be read.</summary>
    PropertyValue? GetProperty(SceneAddress address);

    /// <summary>The active camera's node; null when there is none.</summary>
    EntityGuid? ActiveCamera();

    /// <summary>All or nothing: every command applies, or none does and one failure names the transaction.</summary>
    TransactionTicket SubmitTransaction(IReadOnlyList<Command> commands);

    /// <summary>Refusals of this client's submissions since the last drain.</summary>
    IReadOnlyList<CommandFailure> DrainFailures();

    /// <summary>Every authoring event since the last drain, every client's, in the order applied.</summary>
    IReadOnlyList<Event> DrainEvents();

    /// <summary>Hands the document to the Runtime for reload; null when accepted (Applied follows).</summary>
    ApplyError? Apply();

    /// <summary>Writes a hand-authored document to its source; null on success.</summary>
    SaveError? Save();
}

/// <summary>The real document: the SDK's AuthoringSession over atlantis.remote/1.</summary>
public sealed class RemoteAuthoringDocument : IAuthoringDocument
{
    private readonly AuthoringSession _session;
    private readonly SubscriptionId _subscription;

    public RemoteAuthoringDocument(AuthoringSession session)
    {
        _session = session;
        _subscription = session.Subscribe(new EventFilter());
    }

    public IReadOnlyList<SchemaType> Schema => _session.Schema;

    public DocumentInfo? Info() => _session.Document();

    public IReadOnlyList<NodeSummary> ListNodes()
    {
        var nodes = _session.ListNodes();
        return nodes.IsOk ? nodes.Value! : Array.Empty<NodeSummary>();
    }

    public IReadOnlyList<TypeId> ListComponents(EntityGuid node)
    {
        var components = _session.ListComponents(node);
        return components.IsOk ? components.Value! : Array.Empty<TypeId>();
    }

    public PropertyValue? GetProperty(SceneAddress address)
    {
        var value = _session.GetProperty(address);
        return value.IsOk ? value.Value : null;
    }

    public EntityGuid? ActiveCamera()
    {
        var camera = _session.ActiveCamera();
        return camera.IsOk ? camera.Value : null;
    }

    public TransactionTicket SubmitTransaction(IReadOnlyList<Command> commands) => _session.SubmitTransaction(commands);

    public IReadOnlyList<CommandFailure> DrainFailures() => _session.DrainFailures();

    public IReadOnlyList<Event> DrainEvents()
    {
        var events = _session.DrainEvents(_subscription);
        return events.IsOk ? events.Value! : Array.Empty<Event>();
    }

    public ApplyError? Apply() => _session.Apply();

    public SaveError? Save() => _session.Save();
}

/// <summary>Scene component names, through the authoring schema (Spec 0065 Q8: reflective).</summary>
public static class SceneSchema
{
    public static TypeId? TypeOf(IReadOnlyList<SchemaType> schema, string shortName) =>
        schema.FirstOrDefault(t => t.Kind == TypeKind.Struct && t.ShortName == shortName)?.Id;

    public static string NameOf(IReadOnlyList<SchemaType> schema, TypeId type) =>
        schema.FirstOrDefault(t => t.Id == type)?.ShortName ?? "?";

    /// <summary>The address of a top-level field of a node's component ("Transform", "localPosition").</summary>
    public static SceneAddress? AddressOf(IReadOnlyList<SchemaType> schema, EntityGuid node, string component, string field)
    {
        SchemaType? type = schema.FirstOrDefault(t => t.Kind == TypeKind.Struct && t.ShortName == component);
        SchemaField? leaf = type?.Fields.FirstOrDefault(f => f.Name == field);
        return type is not null && leaf is not null ? new SceneAddress(node, type.Id, leaf.Id) : null;
    }
}
