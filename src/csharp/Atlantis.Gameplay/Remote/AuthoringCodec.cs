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
            default: throw new System.ArgumentException("an unrecognized authoring command", nameof(command));
        }
        return output;
    }

    public static A.Command? DecodeAuthoringCommand(JsonValue value)
    {
        switch (KindOf(value))
        {
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
            default: throw new System.ArgumentException("an unrecognized authoring event", nameof(evt));
        }
        return output;
    }

    public static A.Event? DecodeAuthoringEvent(JsonValue value)
    {
        switch (KindOf(value))
        {
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

    public static JsonValue Encode(A.NodeSummary node) =>
        JsonValue.NewObject().Set("node", Encode(node.Node)).Set("parent", EncodeOptional(node.Parent));

    public static A.NodeSummary? DecodeNodeSummary(JsonValue value)
    {
        (bool ok, EntityGuid? parent) = OptionalEntityMember(value, "parent");
        return NodeOf(value) is EntityGuid node && ok ? new A.NodeSummary(node, parent) : null;
    }

    // --- The error names.

    public static JsonValue Encode(A.AuthoringError error) => JsonValue.String(error.ToString());
    public static A.AuthoringError? DecodeAuthoringError(JsonValue value) => DecodeName<A.AuthoringError>(value);
    public static JsonValue Encode(A.OpenError error) => JsonValue.String(error.ToString());
    public static A.OpenError? DecodeOpenError(JsonValue value) => DecodeName<A.OpenError>(value);
    public static JsonValue Encode(A.SaveError error) => JsonValue.String(error.ToString());
    public static A.SaveError? DecodeSaveError(JsonValue value) => DecodeName<A.SaveError>(value);
    public static JsonValue Encode(A.ApplyError error) => JsonValue.String(error.ToString());
    public static A.ApplyError? DecodeApplyError(JsonValue value) => DecodeName<A.ApplyError>(value);
}
