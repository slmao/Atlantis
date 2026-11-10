// Plan 0065 P6 (Spec 0065 R11; ADR-0130 D3): the wire encoding of the
// authoring document's values (docs/architecture/remote_protocol.md section
// 6.8), mirroring the C++ codec's authoring half (src/remote/src/codec.cpp,
// "AuthoringConnection's values"): encoders build the same JSON, member for
// member, and decoders accept what it accepts and return null for anything
// malformed. Pure functions.
using System.Collections.Generic;
using A = Atlantis.Gameplay.Authoring;

namespace Atlantis.Gameplay.Remote;

public static partial class Codec
{
    private static readonly (A.EventKind Kind, string Name)[] AuthoringEventKinds =
    {
        (A.EventKind.PropertyChanged, "PropertyChanged"),
        (A.EventKind.NodeCreated, "NodeCreated"),
        (A.EventKind.NodeDestroyed, "NodeDestroyed"),
        (A.EventKind.ComponentAdded, "ComponentAdded"),
        (A.EventKind.ComponentRemoved, "ComponentRemoved"),
        (A.EventKind.ParentChanged, "ParentChanged"),
        (A.EventKind.ActiveCameraChanged, "ActiveCameraChanged"),
        (A.EventKind.DocumentReopened, "DocumentReopened"),
        (A.EventKind.Applied, "Applied"),
        (A.EventKind.NodeNameChanged, "NodeNameChanged"),
    };

    // An EntityGuid or null.
    private static JsonValue EncodeOptional(EntityGuid? entity) => entity is EntityGuid e ? Encode(e) : JsonValue.Null();

    // Present and either null or a GUID: (true, value); (false, _) if absent or malformed.
    private static (bool Ok, EntityGuid? Value) OptionalEntityMember(JsonValue value, string key)
    {
        if (value.Find(key) is not JsonValue member) return (false, null);
        if (member.IsNull) return (true, null);
        return DecodeEntity(member) is EntityGuid entity ? (true, entity) : (false, null);
    }

    private static EntityGuid? NodeOf(JsonValue value) => value.Find("node") is JsonValue node ? DecodeEntity(node) : null;

    private static TypeId? ComponentOf(JsonValue value) =>
        value.Find("component") is JsonValue component ? DecodeTypeId(component) : null;

    // --- The scene address, commands, events.

    public static JsonValue Encode(A.SceneAddress address) =>
        JsonValue.NewObject().Set("node", Encode(address.Node)).Set("component", Encode(address.Component))
            .Set("field", Encode(address.Field));

    public static A.SceneAddress? DecodeSceneAddress(JsonValue value)
    {
        if (NodeOf(value) is not EntityGuid node || ComponentOf(value) is not TypeId component ||
            value.Find("field") is not JsonValue field || DecodeFieldId(field) is not FieldId f)
        {
            return null;
        }
        return new A.SceneAddress(node, component, f);
    }

    private static (A.SceneAddress, PropertyValue)? SceneAddressValueOf(JsonValue value)
    {
        if (value.Find("address") is not JsonValue address || value.Find("value") is not JsonValue v) return null;
        if (DecodeSceneAddress(address) is not A.SceneAddress a || DecodePropertyValue(v) is not PropertyValue pv) return null;
        return (a, pv);
    }

    public static JsonValue Encode(A.Command command)
    {
        JsonValue output = JsonValue.NewObject();
        switch (command)
        {
            case A.SetProperty c:
                output.Set("kind", JsonValue.String("SetProperty")).Set("address", Encode(c.Address)).Set("value", Encode(c.Value));
                break;
            case A.CreateNode c:
            {
                JsonValue components = JsonValue.NewArray();
                foreach (TypeId component in c.Components) components.Push(Encode(component));
                output.Set("kind", JsonValue.String("CreateNode")).Set("node", EncodeOptional(c.Node))
                    .Set("position", JsonValue.Number(c.Position)).Set("components", components);
                break;
            }
            case A.DestroyNode c:
                output.Set("kind", JsonValue.String("DestroyNode")).Set("node", Encode(c.Node));
                break;
            case A.AddComponent c:
                output.Set("kind", JsonValue.String("AddComponent")).Set("node", Encode(c.Node)).Set("component", Encode(c.Component));
                break;
            case A.RemoveComponent c:
                output.Set("kind", JsonValue.String("RemoveComponent")).Set("node", Encode(c.Node))
                    .Set("component", Encode(c.Component));
                break;
            case A.SetParent c:
                output.Set("kind", JsonValue.String("SetParent")).Set("node", Encode(c.Node)).Set("parent", EncodeOptional(c.Parent));
                break;
            case A.SetActiveCamera c:
                output.Set("kind", JsonValue.String("SetActiveCamera")).Set("camera", EncodeOptional(c.Camera));
                break;
            case A.SetNodeName c:
                if (!A.NodeNames.IsWellFormed(c.Name)) throw new System.ArgumentException("invalid UTF-16 node name", nameof(command));
                output.Set("kind", JsonValue.String("SetNodeName")).Set("node", Encode(c.Node)).Set("name", JsonValue.String(c.Name));
                break;
            default: throw new System.ArgumentException("an unrecognized authoring command", nameof(command));
        }
        return output;
    }

    public static A.Command? DecodeAuthoringCommand(JsonValue value)
    {
        switch (KindOf(value))
        {
            case "SetNodeName":
                return NodeOf(value) is EntityGuid nn && value.Find("name") is { IsString: true } name && A.NodeNames.IsWellFormed(name.AsString)
                    ? new A.SetNodeName(nn, name.AsString) : null;
            case "SetProperty": return SceneAddressValueOf(value) is var (address, v) ? new A.SetProperty(address, v) : null;
            case "CreateNode":
            {
                (bool ok, EntityGuid? node) = OptionalEntityMember(value, "node");
                if (!ok || value.Find("position") is not JsonValue position || DecodeUInt64(position) is not ulong p ||
                    value.Find("components") is not { IsArray: true } list)
                {
                    return null;
                }
                var components = new List<TypeId>(list.Items.Count);
                foreach (JsonValue item in list.Items)
                {
                    if (DecodeTypeId(item) is not TypeId component) return null;
                    components.Add(component);
                }
                return new A.CreateNode(node, p, components);
            }
            case "DestroyNode": return NodeOf(value) is EntityGuid n ? new A.DestroyNode(n) : null;
            case "AddComponent":
                return NodeOf(value) is EntityGuid an && ComponentOf(value) is TypeId ac ? new A.AddComponent(an, ac) : null;
            case "RemoveComponent":
                return NodeOf(value) is EntityGuid rn && ComponentOf(value) is TypeId rc ? new A.RemoveComponent(rn, rc) : null;
            case "SetParent":
            {
                (bool ok, EntityGuid? parent) = OptionalEntityMember(value, "parent");
                return NodeOf(value) is EntityGuid node && ok ? new A.SetParent(node, parent) : null;
            }
            case "SetActiveCamera":
            {
                (bool ok, EntityGuid? camera) = OptionalEntityMember(value, "camera");
                return ok ? new A.SetActiveCamera(camera) : null;
            }
            default: return null;
        }
    }

    public static JsonValue Encode(A.Event evt)
    {
        JsonValue output = JsonValue.NewObject();
        switch (evt)
        {
            case A.PropertyChanged e:
                output.Set("kind", JsonValue.String("PropertyChanged")).Set("address", Encode(e.Address)).Set("value", Encode(e.Value));
                break;
            case A.NodeCreated e:
                output.Set("kind", JsonValue.String("NodeCreated")).Set("node", Encode(e.Node))
                    .Set("position", JsonValue.Number(e.Position));
                break;
            case A.NodeDestroyed e:
                output.Set("kind", JsonValue.String("NodeDestroyed")).Set("node", Encode(e.Node));
                break;
            case A.ComponentAdded e:
                output.Set("kind", JsonValue.String("ComponentAdded")).Set("node", Encode(e.Node)).Set("component", Encode(e.Component));
                break;
            case A.ComponentRemoved e:
                output.Set("kind", JsonValue.String("ComponentRemoved")).Set("node", Encode(e.Node))
                    .Set("component", Encode(e.Component));
                break;
            case A.ParentChanged e:
                output.Set("kind", JsonValue.String("ParentChanged")).Set("node", Encode(e.Node)).Set("parent", EncodeOptional(e.Parent));
                break;
            case A.ActiveCameraChanged e:
                output.Set("kind", JsonValue.String("ActiveCameraChanged")).Set("camera", EncodeOptional(e.Camera));
                break;
            case A.DocumentReopened e:
                output.Set("kind", JsonValue.String("DocumentReopened")).Set("scene", Encode(e.Scene));
                break;
            case A.Applied e:
                output.Set("kind", JsonValue.String("Applied")).Set("generation", JsonValue.Number(e.Generation));
                break;
            case A.NodeNameChanged e:
                if (!A.NodeNames.IsValid(e.Name)) throw new System.ArgumentException("invalid node name", nameof(evt));
                output.Set("kind", JsonValue.String("NodeNameChanged")).Set("node", Encode(e.Node)).Set("name", JsonValue.String(e.Name));
                break;
            default: throw new System.ArgumentException("an unrecognized authoring event", nameof(evt));
        }
        return output;
    }

    public static A.Event? DecodeAuthoringEvent(JsonValue value)
    {
        switch (KindOf(value))
        {
            case "NodeNameChanged":
                return NodeOf(value) is EntityGuid nn && value.Find("name") is { IsString: true } name && A.NodeNames.IsValid(name.AsString)
                    ? new A.NodeNameChanged(nn, name.AsString) : null;
            case "PropertyChanged": return SceneAddressValueOf(value) is var (address, v) ? new A.PropertyChanged(address, v) : null;
            case "NodeCreated":
                return NodeOf(value) is EntityGuid cn && value.Find("position") is JsonValue position &&
                       DecodeUInt64(position) is ulong p
                    ? new A.NodeCreated(cn, p)
                    : null;
            case "NodeDestroyed": return NodeOf(value) is EntityGuid dn ? new A.NodeDestroyed(dn) : null;
            case "ComponentAdded":
                return NodeOf(value) is EntityGuid an && ComponentOf(value) is TypeId ac ? new A.ComponentAdded(an, ac) : null;
            case "ComponentRemoved":
                return NodeOf(value) is EntityGuid rn && ComponentOf(value) is TypeId rc ? new A.ComponentRemoved(rn, rc) : null;
            case "ParentChanged":
            {
                (bool ok, EntityGuid? parent) = OptionalEntityMember(value, "parent");
                return NodeOf(value) is EntityGuid node && ok ? new A.ParentChanged(node, parent) : null;
            }
            case "ActiveCameraChanged":
            {
                (bool ok, EntityGuid? camera) = OptionalEntityMember(value, "camera");
                return ok ? new A.ActiveCameraChanged(camera) : null;
            }
            case "DocumentReopened":
                return value.Find("scene") is JsonValue scene && DecodeAsset(scene) is AssetGuid s ? new A.DocumentReopened(s) : null;
            case "Applied":
                return value.Find("generation") is JsonValue generation && DecodeUInt64(generation) is ulong g ? new A.Applied(g) : null;
            default: return null;
        }
    }

    // --- Filter, failure, document, node summary.

    public static JsonValue Encode(A.EventFilter filter)
    {
        JsonValue kinds = JsonValue.NewArray();
        foreach ((A.EventKind kind, string name) in AuthoringEventKinds)
        {
            if ((filter.Kinds & kind) != 0) kinds.Push(JsonValue.String(name));
        }
        return JsonValue.NewObject().Set("kinds", kinds).Set("node", EncodeOptional(filter.Node))
            .Set("component", filter.Component is TypeId c ? Encode(c) : JsonValue.Null());
    }

    public static A.EventFilter? DecodeAuthoringFilter(JsonValue value)
    {
        JsonValue? kinds = value.Find("kinds");
        (bool nodeOk, EntityGuid? node) = OptionalEntityMember(value, "node");
        JsonValue? component = value.Find("component");
        if (kinds is null || !kinds.IsArray || !nodeOk || component is null) return null;
        A.EventKind set = A.EventKind.None;
        foreach (JsonValue name in kinds.Items)
        {
            if (!name.IsString) return null;
            bool known = false;
            foreach ((A.EventKind kind, string kindName) in AuthoringEventKinds)
            {
                if (name.AsString == kindName)
                {
                    set |= kind;
                    known = true;
                }
            }
            if (!known) return null;
        }
        TypeId? c = null;
        if (!component.IsNull)
        {
            if (DecodeTypeId(component) is not TypeId decoded) return null;
            c = decoded;
        }
        return new A.EventFilter(set, node, c);
    }

    public static JsonValue Encode(A.CommandFailure failure) =>
        JsonValue.NewObject().Set("ticket", JsonValue.Number(failure.Ticket.Value)).Set("error", Encode(failure.Error));

    public static A.CommandFailure? DecodeAuthoringFailure(JsonValue value)
    {
        if (value.Find("ticket") is not JsonValue ticket || value.Find("error") is not JsonValue error) return null;
        if (DecodeUInt64(ticket) is not ulong t || DecodeAuthoringError(error) is not A.AuthoringError e) return null;
        return new A.CommandFailure(new CommandTicket(t), e);
    }

    public static JsonValue Encode(A.DocumentInfo document) =>
        JsonValue.NewObject().Set("scene", Encode(document.Scene))
            .Set("provenance", JsonValue.String(document.Provenance == A.Provenance.Imported ? "Imported" : "HandAuthored"))
            .Set("source", JsonValue.String(document.SourcePath)).Set("nodeCount", JsonValue.Number(document.NodeCount));

    public static A.DocumentInfo? DecodeDocumentInfo(JsonValue value)
    {
        if (value.Find("scene") is not JsonValue scene || value.Find("provenance") is not { IsString: true } provenance ||
            value.Find("source") is not { IsString: true } source || value.Find("nodeCount") is not JsonValue nodeCount)
        {
            return null;
        }
        if (DecodeAsset(scene) is not AssetGuid s || DecodeUInt64(nodeCount) is not ulong n) return null;
        A.Provenance? p = provenance.AsString switch
        {
            "HandAuthored" => A.Provenance.HandAuthored,
            "Imported" => A.Provenance.Imported,
            _ => null,
        };
        return p is A.Provenance known ? new A.DocumentInfo(s, known, source.AsString, n) : null;
    }

    public static JsonValue Encode(A.NodeSummary node)
    {
        if (!A.NodeNames.IsValid(node.Name)) throw new System.ArgumentException("invalid node name", nameof(node));
        return JsonValue.NewObject().Set("node", Encode(node.Node)).Set("parent", EncodeOptional(node.Parent)).Set("name", JsonValue.String(node.Name));
    }

    public static A.NodeSummary? DecodeNodeSummary(JsonValue value)
    {
        (bool ok, EntityGuid? parent) = OptionalEntityMember(value, "parent");
        JsonValue? member = value.Find("name");
        if (member is not null && (!member.IsString || !A.NodeNames.IsValid(member.AsString))) return null;
        return NodeOf(value) is EntityGuid node && ok ? new A.NodeSummary(node, parent, member?.AsString ?? "") : null;
    }

    // --- The error names.

    public static JsonValue Encode(A.AuthoringError error) => JsonValue.String(error.ToString());
    public static A.AuthoringError? DecodeAuthoringError(JsonValue value) => DecodeName<A.AuthoringError>(value);
    public static JsonValue Encode(A.OpenError error) => JsonValue.String(error.ToString());
    public static A.OpenError? DecodeOpenError(JsonValue value) => DecodeName<A.OpenError>(value);
    public static JsonValue Encode(A.SaveError error) => JsonValue.String(error.ToString());
    public static A.SaveError? DecodeSaveError(JsonValue value) => DecodeName<A.SaveError>(value);
    public static bool ValidSaveAsName(string? name)
    {
        if (name is null || name.Length is < 1 or > 48 || name[0] is < 'a' or > 'z') return false;
        foreach (char c in name)
            if (c is not (>= 'a' and <= 'z') and not (>= '0' and <= '9') and not '_') return false;
        return true;
    }
    public static string SaveAsSource(string name) => "scenes/saved/save_" + name + "/scene.scene.txt";
    public static bool IsLegacySaveAsRefusal(JsonValue reply) =>
        reply.Find("result") is null && reply.Find("error") is { } error &&
        error.Find("code") is { IsString: true, AsString: "BadRequest" } &&
        error.Find("message") is { IsString: true, AsString: "unknown method authoring.saveAs" };
    public static A.SaveAsError? DecodeSaveAsError(JsonValue value) =>
        DecodeName<A.SaveAsError>(value) is { } error && error != A.SaveAsError.Unsupported ? error : null;
    public static JsonValue Encode(A.SavedScene value) => JsonValue.NewObject().Set("scene", Encode(value.Scene))
        .Set("source", JsonValue.String(value.Source)).Set("documentRevision", JsonValue.Number(value.DocumentRevision));
    public static A.SavedScene? DecodeSavedScene(JsonValue value)
    {
        const string prefix = "scenes/saved/save_", suffix = "/scene.scene.txt";
        if (value.Find("scene") is not { IsString: true } scene || DecodeAsset(scene) is not AssetGuid guid || guid.IsNil ||
            scene.AsString != guid.ToString() || value.Find("source") is not { IsString: true } source ||
            value.Find("documentRevision") is not { } revision || DecodeUInt64(revision) is not ulong number) return null;
        string path = source.AsString;
        return path.Length > prefix.Length + suffix.Length && path.StartsWith(prefix, System.StringComparison.Ordinal) &&
            path.EndsWith(suffix, System.StringComparison.Ordinal) && ValidSaveAsName(path[prefix.Length..^suffix.Length])
            ? new A.SavedScene(guid, path, number) : null;
    }
    public static bool IsLegacyCreateSceneRefusal(JsonValue reply) =>
        reply.Find("result") is null && reply.Find("error") is { } error &&
        error.Find("code") is { IsString: true, AsString: "BadRequest" } &&
        error.Find("message") is { IsString: true, AsString: "unknown method authoring.createScene" };
    public static JsonValue Encode(A.CreateSceneError error) => JsonValue.String(error.ToString());
    public static A.CreateSceneError? DecodeCreateSceneError(JsonValue value) =>
        DecodeName<A.CreateSceneError>(value) is { } error && error is not (A.CreateSceneError.Unsupported or A.CreateSceneError.NoAuthoring) ? error : null;
    public static JsonValue Encode(A.CreatedScene value) => JsonValue.NewObject().Set("scene", Encode(value.Scene))
        .Set("source", JsonValue.String(value.Source));
    public static A.CreatedScene? DecodeCreatedScene(JsonValue value)
    {
        const string prefix = "scenes/saved/save_", suffix = "/scene.scene.txt";
        if (value.Find("scene") is not { IsString: true } scene || DecodeAsset(scene) is not AssetGuid guid || guid.IsNil ||
            scene.AsString != guid.ToString() || value.Find("source") is not { IsString: true } source) return null;
        string path = source.AsString;
        return path.Length > prefix.Length + suffix.Length && path.StartsWith(prefix, System.StringComparison.Ordinal) &&
            path.EndsWith(suffix, System.StringComparison.Ordinal) && ValidSaveAsName(path[prefix.Length..^suffix.Length])
            ? new A.CreatedScene(guid, path) : null;
    }
    public static JsonValue Encode(A.ApplyError error) => JsonValue.String(error.ToString());
    public static A.ApplyError? DecodeApplyError(JsonValue value) => DecodeName<A.ApplyError>(value);

    public static JsonValue Encode(A.ApplyReceipt receipt) => JsonValue.NewObject()
        .Set("id", JsonValue.String(receipt.Id.ToString())).Set("documentRevision", JsonValue.Number(receipt.DocumentRevision));
    public static A.ApplyReceipt? DecodeApplyReceipt(JsonValue value)
    {
        if (value.Find("id") is not { IsString: true } id || value.Find("documentRevision") is not JsonValue revision ||
            !A.ApplyId.TryParse(id.AsString, out A.ApplyId parsed) || DecodeUInt64(revision) is not ulong number || number == 0) return null;
        return new A.ApplyReceipt(parsed, number);
    }
    public static JsonValue Encode(A.ApplyRecord record)
    {
        JsonValue value = JsonValue.NewObject().Set("receipt", Encode(record.Receipt)).Set("state", JsonValue.String(record.State.ToString()));
        if (record.Generation is ulong gen) value.Set("generation", JsonValue.Number(gen));
        if (record.Error is A.ReloadError error) value.Set("error", JsonValue.String(error.ToString()));
        if (record.SupersededBy is A.ApplyId id) value.Set("supersededBy", JsonValue.String(id.ToString()));
        return value;
    }
    public static A.ApplyRecord? DecodeApplyRecord(JsonValue value)
    {
        if (value.Find("receipt") is not JsonValue receipt || DecodeApplyReceipt(receipt) is not A.ApplyReceipt decoded ||
            value.Find("state") is not JsonValue state || DecodeName<A.ApplyState>(state) is not A.ApplyState named) return null;
        JsonValue? gen = value.Find("generation"), error = value.Find("error"), by = value.Find("supersededBy");
        if ((named != A.ApplyState.Applied && gen is not null) || (named != A.ApplyState.Failed && error is not null) ||
            (named != A.ApplyState.Superseded && by is not null)) return null;
        ulong? generation = null; A.ReloadError? refusal = null; A.ApplyId? successor = null;
        if (named == A.ApplyState.Applied)
        {
            generation = gen is null ? null : DecodeUInt64(gen); if (generation is null or 0) return null;
        }
        else if (named == A.ApplyState.Failed)
        {
            refusal = error is null ? null : DecodeName<A.ReloadError>(error); if (refusal is null) return null;
        }
        else if (named == A.ApplyState.Superseded)
        {
            if (by is not { IsString: true } || !A.ApplyId.TryParse(by.AsString, out A.ApplyId id) || id == decoded.Id) return null;
            successor = id;
        }
        return new A.ApplyRecord(decoded, named, generation, refusal, successor);
    }
    public static JsonValue Encode(A.ApplySnapshot snapshot) => JsonValue.NewObject()
        .Set("currentDocumentRevision", snapshot.CurrentDocumentRevision is ulong revision ? JsonValue.Number(revision) : JsonValue.Null())
        .Set("request", snapshot.Request is A.ApplyRecord request ? Encode(request) : JsonValue.Null())
        .Set("projection", snapshot.Projection is A.ApplyProjection projection ? JsonValue.NewObject()
            .Set("receipt", Encode(projection.Receipt)).Set("generation", JsonValue.Number(projection.Generation)) : JsonValue.Null());
    public static A.ApplySnapshot? DecodeApplySnapshot(JsonValue value)
    {
        if (value.Find("currentDocumentRevision") is not JsonValue revision || value.Find("request") is not JsonValue request ||
            value.Find("projection") is not JsonValue projected) return null;
        ulong? current = revision.IsNull ? null : DecodeUInt64(revision);
        if (!revision.IsNull && current is null or 0) return null;
        A.ApplyRecord? record = request.IsNull ? null : DecodeApplyRecord(request);
        if (!request.IsNull && record is null) return null;
        A.ApplyProjection? projection = null;
        if (!projected.IsNull)
        {
            if (projected.Find("receipt") is not JsonValue receipt || DecodeApplyReceipt(receipt) is not A.ApplyReceipt decoded ||
                projected.Find("generation") is not JsonValue gen || DecodeUInt64(gen) is not ulong number || number == 0) return null;
            projection = new A.ApplyProjection(decoded, number);
        }
        if ((current is null && (record is not null || projection is not null)) || (record is null && projection is not null)) return null;
        if (current is ulong c && ((record is not null && record.Receipt.DocumentRevision > c) ||
            (projection is not null && projection.Receipt.DocumentRevision > c))) return null;
        if (record is not null && projection is not null && record.Receipt.Id == projection.Receipt.Id &&
            (record.State != A.ApplyState.Applied || record.Receipt != projection.Receipt || record.Generation != projection.Generation)) return null;
        return new A.ApplySnapshot(current, record, projection);
    }
    public static JsonValue Encode(A.ApplyLookupError error) => JsonValue.String(error.ToString());
    public static A.ApplyLookupError? DecodeApplyLookupError(JsonValue value) => DecodeName<A.ApplyLookupError>(value);
}
