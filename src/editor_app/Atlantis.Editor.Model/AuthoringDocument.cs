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

    /// <summary>Several nodes' component types in the input's order; a document that can pipeline the reads overrides it.</summary>
    IReadOnlyList<IReadOnlyList<TypeId>> ListComponents(IReadOnlyList<EntityGuid> nodes) => nodes.Select(ListComponents).ToList();

    /// <summary>A property's value; null when it cannot be read.</summary>
    PropertyValue? GetProperty(SceneAddress address);

    /// <summary>Several properties in the input's order (null where one cannot be read); a document that can pipeline the reads overrides it.</summary>
    IReadOnlyList<PropertyValue?> GetProperties(IReadOnlyList<SceneAddress> addresses) => addresses.Select(GetProperty).ToList();

    /// <summary>The active camera's node; null when there is none.</summary>
    EntityGuid? ActiveCamera();

    /// <summary>All or nothing: every command applies, or none does and one failure names the transaction.</summary>
    TransactionTicket SubmitTransaction(IReadOnlyList<Command> commands);

    /// <summary>Refusals of this client's submissions since the last drain.</summary>
    IReadOnlyList<CommandFailure> DrainFailures();

    /// <summary>
    /// Submit, drain the failures and apply as one: a document that can pipeline the three overrides it, so they share a
    /// Runtime frame boundary (J8). The default is the three calls in turn, applying only when nothing was refused.
    /// </summary>
    CommitOutcome CommitAndApply(IReadOnlyList<Command> commands)
    {
        SubmitTransaction(commands);
        IReadOnlyList<CommandFailure> failures = DrainFailures();
        if (failures.Count > 0) return new CommitOutcome(failures, false, null);
        return new CommitOutcome(failures, true, Apply());
    }

    /// <summary>Every authoring event since the last drain, every client's, in the order applied.</summary>
    IReadOnlyList<Event> DrainEvents();

    /// <summary>Hands the document to the Runtime for reload; null when accepted (Applied follows).</summary>
    ApplyError? Apply();

    /// <summary>Writes a hand-authored document to its source; null on success.</summary>
    SaveError? Save();
}

/// <summary>A commit's refusals, whether an apply was sent with it, and that apply's refusal.</summary>
public sealed record CommitOutcome(IReadOnlyList<CommandFailure> Failures, bool ApplySent, ApplyError? ApplyError);

/// <summary>The real document: the SDK's AuthoringSession over atlantis.remote/1.</summary>
public sealed class RemoteAuthoringDocument : IAuthoringDocument
{
    private readonly AuthoringSession _session;
    private readonly SubscriptionId _subscription;
    private readonly RemoteSession? _wire;

    // Answered at a Runtime frame boundary each (~47 ms on Bistro), so reading a big document call by call is minutes;
    // with the wire the batched reads share a boundary. No new wire call (ADR-0125 D2).
    private const int PipelineWindow = 256;

    public RemoteAuthoringDocument(AuthoringSession session, RemoteSession? wire = null)
    {
        _session = session;
        _wire = wire;
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

    public IReadOnlyList<IReadOnlyList<TypeId>> ListComponents(IReadOnlyList<EntityGuid> nodes)
    {
        if (_wire is null) return nodes.Select(ListComponents).ToList();
        var all = new List<IReadOnlyList<TypeId>>(nodes.Count);
        for (int start = 0; start < nodes.Count; start += PipelineWindow)
        {
            int count = Math.Min(PipelineWindow, nodes.Count - start);
            var ids = new List<ulong>(count);
            for (int i = 0; i < count; ++i)
            {
                ids.Add(_wire.Send("authoring.listComponents", JsonValue.NewObject().Set("node", Codec.Encode(nodes[start + i]))));
            }
            foreach (ulong id in ids)
            {
                JsonValue result = _wire.AwaitResult(id);
                var types = new List<TypeId>();
                if (result.Find("ok") is JsonValue ok)
                {
                    foreach (JsonValue item in ok.Items)
                    {
                        if (Codec.DecodeTypeId(item) is TypeId type) types.Add(type);
                    }
                }
                all.Add(types);
            }
        }
        return all;
    }

    public IReadOnlyList<PropertyValue?> GetProperties(IReadOnlyList<SceneAddress> addresses)
    {
        if (_wire is null) return addresses.Select(GetProperty).ToList();
        var all = new List<PropertyValue?>(addresses.Count);
        for (int start = 0; start < addresses.Count; start += PipelineWindow)
        {
            int count = Math.Min(PipelineWindow, addresses.Count - start);
            var ids = new List<ulong>(count);
            for (int i = 0; i < count; ++i)
            {
                ids.Add(_wire.Send("authoring.getProperty", JsonValue.NewObject().Set("address", Codec.Encode(addresses[start + i]))));
            }
            foreach (ulong id in ids)
            {
                JsonValue result = _wire.AwaitResult(id);
                all.Add(result.Find("ok") is JsonValue ok ? Codec.DecodePropertyValue(ok) : null);
            }
        }
        return all;
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

    public CommitOutcome CommitAndApply(IReadOnlyList<Command> commands)
    {
        if (_wire is null)
        {
            SubmitTransaction(commands);
            IReadOnlyList<CommandFailure> failures = DrainFailures();
            return failures.Count > 0 ? new CommitOutcome(failures, false, null) : new CommitOutcome(failures, true, Apply());
        }
        // A refused submit still gets its apply: the document is then unchanged, so the reload is the same scene again.
        JsonValue list = JsonValue.NewArray();
        foreach (Command command in commands) list.Push(Codec.Encode(command));
        ulong submit = _wire.Send("authoring.submitTransaction", JsonValue.NewObject().Set("commands", list));
        ulong drain = _wire.Send("authoring.drainFailures");
        ulong apply = _wire.Send("authoring.apply");
        _wire.AwaitResult(submit);
        var refused = new List<CommandFailure>();
        foreach (JsonValue item in _wire.AwaitResult(drain).Items)
        {
            if (Codec.DecodeAuthoringFailure(item) is CommandFailure failure) refused.Add(failure);
        }
        JsonValue applied = _wire.AwaitResult(apply);
        ApplyError? error = applied.Find("err") is JsonValue err ? Codec.DecodeApplyError(err) : null;
        return new CommitOutcome(refused, true, error);
    }

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
