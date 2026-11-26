// Plan 0065 P6 (Spec 0065 R11; ADR-0130 D3): the wire encoding of the
// authoring document's values (docs/architecture/remote_protocol.md section
// 6.8), mirroring the C++ codec's authoring half (src/remote/src/codec.cpp,
// "AuthoringConnection's values"): encoders build the same JSON, member for
// member, and decoders accept what it accepts and return null for anything
// malformed. Pure functions.
using System.Collections.Generic;
using System.Text;
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
        (A.EventKind.NodeOrderChanged, "NodeOrderChanged"),
        (A.EventKind.NodeMovedInSiblingOrder, "NodeMovedInSiblingOrder"),
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
            case A.SwapAdjacentSiblings c:
                output.Set("kind", JsonValue.String("SwapAdjacentSiblings")).Set("node", Encode(c.Node))
                    .Set("sibling", Encode(c.Sibling)).Set("parent", EncodeOptional(c.Parent))
                    .Set("direction", JsonValue.String(c.Direction.ToString())).Set("expectedRevision", JsonValue.Number(c.ExpectedRevision));
                break;
            case A.MoveSiblingRelative c:
                output.Set("kind", JsonValue.String("MoveSiblingRelative")).Set("node", Encode(c.Node))
                    .Set("anchor", Encode(c.Anchor)).Set("parent", EncodeOptional(c.Parent))
                    .Set("placement", JsonValue.String(c.Placement.ToString()))
                    .Set("expectedRevision", JsonValue.Number(c.ExpectedRevision));
                break;
            case A.RequireDocumentRevision c:
                output.Set("kind", JsonValue.String("RequireDocumentRevision"))
                    .Set("expectedRevision", JsonValue.Number(c.ExpectedRevision));
                break;
            case A.RequireNameBatchDocument c:
                output.Set("kind", JsonValue.String("RequireNameBatchDocument"))
                    .Set("expectedDocument", Encode(c.ExpectedDocument));
                break;
            case A.RequireRenderLookDocument c:
                var roster = JsonValue.NewArray();
                foreach (var light in c.Basis.LightRoster) roster.Push(Encode(light));
                output.Set("kind", JsonValue.String("RequireRenderLookDocument"))
                    .Set("expectedDocument", Encode(c.ExpectedDocument)).Set("activeCamera", Encode(c.Basis.ActiveCamera))
                    .Set("lightRoster", roster).Set("placementSignature", JsonValue.String(c.Basis.PlacementSignature));
                break;
            case A.RequireDuplicateBatchRevision c:
                output.Set("kind", JsonValue.String("RequireDuplicateBatchRevision"))
                    .Set("expectedRevision", JsonValue.Number(c.ExpectedRevision));
                break;
            case A.RequireTransformBatchRevision c:
                output.Set("kind", JsonValue.String("RequireTransformBatchRevision"))
                    .Set("expectedRevision", JsonValue.Number(c.ExpectedRevision));
                break;
            case A.SetActiveCamera c:
                output.Set("kind", JsonValue.String("SetActiveCamera")).Set("camera", EncodeOptional(c.Camera));
                break;
            case A.SetPrefabOrigin c:
                output.Set("kind", JsonValue.String("SetPrefabOrigin")).Set("node", Encode(c.Node)).Set("prefab", Encode(c.Prefab));
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
            case "SetPrefabOrigin":
                return NodeOf(value) is EntityGuid pn && value.Find("prefab") is { } prefab &&
                    DecodeAsset(prefab) is AssetGuid origin && !origin.IsNil ? new A.SetPrefabOrigin(pn, origin) : null;
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
            case "SwapAdjacentSiblings":
            {
                var (parentOk, parent) = OptionalEntityMember(value, "parent");
                var (siblingOk, sibling) = OptionalEntityMember(value, "sibling");
                return NodeOf(value) is EntityGuid node && siblingOk && sibling is EntityGuid other && parentOk &&
                       value.Find("direction") is { IsString: true } direction &&
                       direction.AsString is "Up" or "Down" &&
                       value.Find("expectedRevision") is JsonValue revision && DecodeUInt64(revision) is ulong expected
                    ? new A.SwapAdjacentSiblings(node, other, parent,
                        direction.AsString == "Up" ? A.SiblingDirection.Up : A.SiblingDirection.Down, expected)
                    : null;
            }
            case "MoveSiblingRelative":
            {
                var (parentOk, parent) = OptionalEntityMember(value, "parent");
                var (anchorOk, anchor) = OptionalEntityMember(value, "anchor");
                return NodeOf(value) is EntityGuid node && anchorOk && anchor is EntityGuid other && parentOk &&
                       value.Find("placement") is { IsString: true } placement &&
                       placement.AsString is "Before" or "After" &&
                       value.Find("expectedRevision") is JsonValue revision && DecodeUInt64(revision) is ulong expected
                    ? new A.MoveSiblingRelative(node, other, parent,
                        placement.AsString == "Before" ? A.SiblingPlacement.Before : A.SiblingPlacement.After, expected)
                    : null;
            }
            case "RequireDocumentRevision":
                return value.Find("expectedRevision") is JsonValue required && DecodeUInt64(required) is ulong guardedRevision
                    ? new A.RequireDocumentRevision(guardedRevision) : null;
            case "RequireNameBatchDocument":
                return Exact(value, 2) && value.Find("expectedDocument") is { } nameDocument && Exact(nameDocument, 2) &&
                    DecodeDocumentToken(nameDocument) is { } token ? new A.RequireNameBatchDocument(token) : null;
            case "RequireRenderLookDocument":
            {
                if (!Exact(value, 5) || value.Find("expectedDocument") is not { } lookDocument ||
                    !Exact(lookDocument, 2) || DecodeDocumentToken(lookDocument) is not { } expected ||
                    value.Find("activeCamera") is not { } active || DecodeEntity(active) is not EntityGuid camera ||
                    value.Find("lightRoster") is not { Kind: JsonKind.Array } roster ||
                    roster.Items.Count > A.RequireRenderLookDocument.MaxLights ||
                    value.Find("placementSignature") is not { IsString: true } signature ||
                    signature.AsString.Length == 0 ||
                    System.Text.Encoding.UTF8.GetByteCount(signature.AsString) > A.RequireRenderLookDocument.MaxPlacementBytes)
                    return null;
                List<EntityGuid> lights = new(roster.Items.Count);
                string? previous = null;
                foreach (var entry in roster.Items)
                {
                    if (DecodeEntity(entry) is not EntityGuid light || light == camera) return null;
                    string id = light.ToString();
                    if (previous is not null && string.CompareOrdinal(previous, id) >= 0) return null;
                    lights.Add(light);
                    previous = id;
                }
                return new A.RequireRenderLookDocument(expected, new A.RenderLookBasis(camera, lights, signature.AsString));
            }
            case "RequireDuplicateBatchRevision":
                return value.Find("expectedRevision") is JsonValue duplicateGuard && DecodeUInt64(duplicateGuard) is ulong duplicateRevision
                    ? new A.RequireDuplicateBatchRevision(duplicateRevision) : null;
            case "RequireTransformBatchRevision":
                return value.Find("expectedRevision") is JsonValue transformGuard && DecodeUInt64(transformGuard) is ulong transformRevision
                    ? new A.RequireTransformBatchRevision(transformRevision) : null;
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
                if (e.PrefabOrigin is AssetGuid origin) output.Set("prefabOrigin", Encode(origin));
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
            case A.NodeOrderChanged e:
                output.Set("kind", JsonValue.String("NodeOrderChanged")).Set("node", Encode(e.Node))
                    .Set("sibling", Encode(e.Sibling)).Set("position", JsonValue.Number(e.Position))
                    .Set("siblingPosition", JsonValue.Number(e.SiblingPosition)).Set("revision", JsonValue.Number(e.Revision));
                break;
            case A.NodeMovedInSiblingOrder e:
                output.Set("kind", JsonValue.String("NodeMovedInSiblingOrder")).Set("node", Encode(e.Node))
                    .Set("parent", EncodeOptional(e.Parent)).Set("fromPosition", JsonValue.Number(e.FromPosition))
                    .Set("position", JsonValue.Number(e.Position)).Set("revision", JsonValue.Number(e.Revision));
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
            case "NodeOrderChanged":
            {
                var (siblingOk, sibling) = OptionalEntityMember(value, "sibling");
                return NodeOf(value) is EntityGuid node && siblingOk && sibling is EntityGuid other &&
                       value.Find("position") is JsonValue orderPosition && DecodeUInt64(orderPosition) is ulong slot &&
                       value.Find("siblingPosition") is JsonValue siblingPosition && DecodeUInt64(siblingPosition) is ulong otherSlot &&
                       value.Find("revision") is JsonValue revision && DecodeUInt64(revision) is ulong committed
                    ? new A.NodeOrderChanged(node, other, slot, otherSlot, committed) : null;
            }
            case "NodeMovedInSiblingOrder":
            {
                var (parentOk, parent) = OptionalEntityMember(value, "parent");
                return NodeOf(value) is EntityGuid node && parentOk &&
                       value.Find("fromPosition") is JsonValue from && DecodeUInt64(from) is ulong oldSlot &&
                       value.Find("position") is JsonValue movePosition && DecodeUInt64(movePosition) is ulong newSlot &&
                       value.Find("revision") is JsonValue revision && DecodeUInt64(revision) is ulong committed &&
                       oldSlot != newSlot && committed != 0
                    ? new A.NodeMovedInSiblingOrder(node, parent, oldSlot, newSlot, committed) : null;
            }
            case "PropertyChanged": return SceneAddressValueOf(value) is var (address, v) ? new A.PropertyChanged(address, v) : null;
            case "NodeCreated":
            {
                if (!TryPrefabOrigin(value, out AssetGuid? origin)) return null;
                return NodeOf(value) is EntityGuid cn && value.Find("position") is JsonValue position &&
                       DecodeUInt64(position) is ulong p
                    ? new A.NodeCreated(cn, p, origin)
                    : null;
            }
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

    public static bool IsLegacySiblingOrderRefusal(JsonValue response) =>
        response.Find("result") is null && response.Find("error") is { IsObject: true } error && error.Members.Count == 2 &&
        error.Find("code") is { IsString: true } code && code.AsString == "BadRequest" &&
        error.Find("message") is { IsString: true } message && message.AsString == "malformed or missing filter";

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

    public static JsonValue Encode(A.DocumentInfo document)
    {
        JsonValue result = JsonValue.NewObject().Set("scene", Encode(document.Scene))
            .Set("provenance", JsonValue.String(document.Provenance == A.Provenance.Imported ? "Imported" : "HandAuthored"))
            .Set("source", JsonValue.String(document.SourcePath)).Set("nodeCount", JsonValue.Number(document.NodeCount));
        if (document.SupportsGuardedPropertyTransactions)
            result.Set("supportsGuardedPropertyTransactions", JsonValue.Bool(true));
        if (document.SupportsGuardedTransformTransactions)
            result.Set("supportsGuardedTransformTransactions", JsonValue.Bool(true));
        if (document.SupportsGuardedDuplicateTransactions)
            result.Set("supportsGuardedDuplicateTransactions", JsonValue.Bool(true));
        if (document.SupportsGuardedNameTransactions) result.Set("supportsGuardedNameTransactions", JsonValue.Bool(true));
        if (document.SupportsGuardedRenderLookTransactions)
            result.Set("supportsGuardedRenderLookTransactions", JsonValue.Bool(true));
        if (document.SupportsPrefabOrigin) result.Set("supportsPrefabOrigin", JsonValue.Bool(true));
        if (document.SupportsPrefabSync) result.Set("supportsPrefabSync", JsonValue.Bool(true));
        if (document.SupportsPrefabRegistration) result.Set("supportsPrefabRegistration", JsonValue.Bool(true));
        return result;
    }

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
        return p is A.Provenance known ? new A.DocumentInfo(s, known, source.AsString, n)
        {
            SupportsGuardedPropertyTransactions =
                value.Find("supportsGuardedPropertyTransactions") is { Kind: JsonKind.Bool, AsBool: true },
            SupportsGuardedTransformTransactions =
                value.Find("supportsGuardedTransformTransactions") is { Kind: JsonKind.Bool, AsBool: true },
            SupportsGuardedDuplicateTransactions =
                value.Find("supportsGuardedDuplicateTransactions") is { Kind: JsonKind.Bool, AsBool: true },
            SupportsGuardedNameTransactions = p == A.Provenance.HandAuthored &&
                value.Find("supportsGuardedNameTransactions") is { Kind: JsonKind.Bool, AsBool: true },
            SupportsGuardedRenderLookTransactions = p == A.Provenance.HandAuthored &&
                value.Find("supportsGuardedRenderLookTransactions") is { Kind: JsonKind.Bool, AsBool: true },
            SupportsPrefabOrigin = value.Find("supportsPrefabOrigin") is { Kind: JsonKind.Bool, AsBool: true },
            SupportsPrefabSync = value.Find("supportsPrefabSync") is { Kind: JsonKind.Bool, AsBool: true },
            SupportsPrefabRegistration = value.Find("supportsPrefabRegistration") is { Kind: JsonKind.Bool, AsBool: true }
        } : null;
    }

    public static JsonValue Encode(A.NodeSummary node)
    {
        if (!A.NodeNames.IsValid(node.Name)) throw new System.ArgumentException("invalid node name", nameof(node));
        JsonValue result = JsonValue.NewObject().Set("node", Encode(node.Node)).Set("parent", EncodeOptional(node.Parent))
            .Set("name", JsonValue.String(node.Name));
        if (node.PrefabOrigin is AssetGuid origin) result.Set("prefabOrigin", Encode(origin));
        if (node.PrefabBaseline is string baseline) result.Set("prefabBaseline", JsonValue.String(baseline));
        return result;
    }

    public static A.NodeSummary? DecodeNodeSummary(JsonValue value)
    {
        (bool ok, EntityGuid? parent) = OptionalEntityMember(value, "parent");
        JsonValue? member = value.Find("name");
        if (member is not null && (!member.IsString || !A.NodeNames.IsValid(member.AsString))) return null;
        if (NodeOf(value) is not EntityGuid node || !ok || !TryPrefabOrigin(value, out AssetGuid? origin)) return null;
        JsonValue? baseline = value.Find("prefabBaseline");
        if (baseline is not null && (origin is null || !baseline.IsString ||
            CountMember(value, "prefabBaseline") != 1 || !ValidPrefabDigest(baseline.AsString))) return null;
        return new A.NodeSummary(node, parent, member?.AsString ?? "", origin, baseline?.AsString);
    }

    private static bool TryPrefabOrigin(JsonValue value, out AssetGuid? origin)
    {
        JsonValue? field = value.Find("prefabOrigin");
        origin = field is null ? null : DecodeAsset(field);
        return CountMember(value, "prefabOrigin") <= 1 && (field is null || origin is { IsNil: false });
    }

    // --- The error names.

    public static JsonValue Encode(A.AuthoringError error) => JsonValue.String(error.ToString());
    public static A.AuthoringError? DecodeAuthoringError(JsonValue value) => DecodeName<A.AuthoringError>(value);
    public static JsonValue Encode(A.OpenError error) => JsonValue.String(error.ToString());
    public static A.OpenError? DecodeOpenError(JsonValue value) => DecodeName<A.OpenError>(value);
    public static JsonValue Encode(A.DocumentToken token) => JsonValue.NewObject()
        .Set("documentId", Encode(token.DocumentId)).Set("documentRevision", JsonValue.Number(token.DocumentRevision));

    public static A.DocumentToken? DecodeDocumentToken(JsonValue value)
    {
        if (value.Find("documentId") is not { IsString: true } id || DecodeEntity(id) is not EntityGuid guid ||
            guid.IsNil || guid.ToString() != id.AsString || value.Find("documentRevision") is not { } revision ||
            DecodeUInt64(revision) is not ulong number || number == 0) return null;
        return new(guid, number);
    }

    public static A.DocumentState? DecodeDocumentState(JsonValue value)
    {
        if (!value.IsObject || value.Members.Count != 7 || DecodeDocumentToken(value) is not { } token ||
            value.Find("scene") is not { IsString: true } scene || DecodeAsset(scene) is not AssetGuid guid ||
            guid.IsNil || guid.ToString() != scene.AsString ||
            value.Find("source") is not { IsString: true } source || source.AsString.Length == 0 ||
            value.Find("provenance") is not { IsString: true } provenance ||
            value.Find("lastPersistedRevision") is not { } last || DecodeUInt64(last) is not ulong persisted ||
            value.Find("dirty") is not { IsBool: true } dirty || persisted == 0 || persisted > token.DocumentRevision ||
            (persisted == token.DocumentRevision && dirty.AsBool)) return null;
        return provenance.AsString switch
        {
            "HandAuthored" => new(token, guid, source.AsString, A.Provenance.HandAuthored, persisted, dirty.AsBool),
            "Imported" => new(token, guid, source.AsString, A.Provenance.Imported, persisted, dirty.AsBool),
            _ => null
        };
    }

    public static bool ValidDocumentReceipt(A.DocumentState state, A.DocumentToken? token, bool saving)
    {
        if (state.Dirty || state.LastPersistedRevision != state.Token.DocumentRevision) return false;
        if (saving) return state.Token == token && state.Provenance == A.Provenance.HandAuthored;
        ulong before = token?.DocumentRevision ?? 0;
        return state.Token.DocumentId != token?.DocumentId && state.Token.DocumentRevision > before &&
            state.Token.DocumentRevision - before == 1;
    }

    public static A.DocumentError? DecodeDocumentError(JsonValue value, bool saving) =>
        DecodeName<A.DocumentError>(value) switch
        {
            A.DocumentError.NoDocument or A.DocumentError.StaleDocument => DecodeName<A.DocumentError>(value),
            A.DocumentError.ImportedSceneNotSavable or A.DocumentError.WriteFailed when saving => DecodeName<A.DocumentError>(value),
            A.DocumentError.SourceUnreadable or A.DocumentError.SourceParseFailed or A.DocumentError.SourceInvalid when !saving => DecodeName<A.DocumentError>(value),
            _ => null
        };

    public static A.DocumentError? DocumentRpcRefusal(JsonValue reply, string method)
    {
        if (reply.Find("result") is not null || reply.Find("error") is not { } error) return null;
        if (error.Find("code") is { IsString: true, AsString: "NoAuthoring" }) return A.DocumentError.NoAuthoring;
        if (error.Find("code") is not { IsString: true, AsString: "BadRequest" } ||
            error.Find("message") is not { IsString: true } message) return null;
        return message.AsString == "unknown method " + method ? A.DocumentError.Unsupported : A.DocumentError.InvalidRequest;
    }

    public static JsonValue? DocumentResult(JsonValue reply) =>
        reply.Find("error") is null && reply.Find("result") is { IsObject: true } result && result.Members.Count == 1 &&
        ((result.Find("ok") is null) != (result.Find("err") is null)) ? result : null;

    public static A.RecoveryError? RecoveryRpcRefusal(JsonValue reply, string method) =>
        DocumentRpcRefusal(reply, method) switch
        {
            A.DocumentError.Unsupported => A.RecoveryError.Unsupported,
            A.DocumentError.NoAuthoring => A.RecoveryError.NoAuthoring,
            A.DocumentError.InvalidRequest => A.RecoveryError.InvalidRequest,
            _ => null,
        };

    public static A.RecoveryError? DecodeRecoveryError(JsonValue value) => DecodeName<A.RecoveryError>(value) switch
    {
        A.RecoveryError.Unsupported or A.RecoveryError.NoAuthoring or A.RecoveryError.InvalidRequest => null,
        var error => error,
    };

    private static ulong? RecoveryNumber(JsonValue obj, string member) =>
        obj.Find(member) is { } field ? DecodeUInt64(field) : null;

    private static string[]? DecodeRecoveryLines(JsonValue? list)
    {
        if (list is not { IsArray: true } || list.Items.Count > 20) return null;
        string[] lines = new string[list.Items.Count];
        for (int i = 0; i < lines.Length; ++i)
        {
            if (!list.Items[i].IsString) return null;
            string text = list.Items[i].AsString;
            if (text.Contains('\r') || text.Contains('\n')) return null;
            int scalars = 0;
            foreach (Rune scalar in text.EnumerateRunes()) if (++scalars > 256) return null;
            lines[i] = text;
        }
        return lines;
    }

    public static A.RecoveryStatus? DecodeRecoveryStatus(JsonValue value, EntityGuid? requested)
    {
        if (!value.IsObject || value.Members.Count != 4 || value.Find("capture") is not { } capture ||
            DecodeName<A.RecoveryCapture>(capture) is not { } phase || value.Find("protectedRevision") is not { } protectedValue ||
            value.Find("copy") is not { } copy || value.Find("preview") is not { } preview) return null;
        ulong? protectedRevision = protectedValue.IsNull ? null : DecodeUInt64(protectedValue);
        if (!protectedValue.IsNull && (protectedRevision is null or 0)) return null;
        A.RecoveryCopy? known = null;
        if (!copy.IsNull)
        {
            if (!copy.IsObject || copy.Members.Count != 4 || copy.Find("id") is not { } id ||
                DecodeEntity(id) is not { IsNil: false } guid || guid.ToString() != id.AsString ||
                RecoveryNumber(copy, "revision") is not ulong revision || revision == 0 ||
                RecoveryNumber(copy, "capturedUnixSeconds") is not ulong time || time == 0 || time > 253402300799UL ||
                copy.Find("pending") is not { IsBool: true } pending) return null;
            known = new(guid, revision, time, pending.AsBool);
        }
        if (protectedRevision is not null && known is not { Pending: false }) return null;
        if (requested is null) return preview.IsNull ? new(phase, protectedRevision, known, null) : null;
        if (known is not { Pending: true } || known.Id != requested || !preview.IsObject ||
            preview.Members.Count != 6 || preview.Find("source") is not { } sourceValue ||
            DecodeName<A.RecoverySource>(sourceValue) is not { } source ||
            RecoveryNumber(preview, "copyNodes") is not { } copyNodes || preview.Find("sourceNodes") is not { } official ||
            DecodeRecoveryLines(preview.Find("sourceLines")) is not { } before ||
            DecodeRecoveryLines(preview.Find("copyLines")) is not { } after ||
            preview.Find("truncated") is not { IsBool: true } truncated) return null;
        ulong? sourceNodes = official.IsNull ? null : DecodeUInt64(official);
        if ((source == A.RecoverySource.Unreadable) != official.IsNull || (!official.IsNull && sourceNodes is null)) return null;
        return new(phase, protectedRevision, known, new(source, copyNodes, sourceNodes, before, after, truncated.AsBool));
    }

    public static A.RecoveryReceipt? DecodeRecoveryReceipt(JsonValue value, A.DocumentToken token, EntityGuid id, bool restoring)
    {
        if (!value.IsObject || value.Members.Count != 2 || value.Find("copyId") is not { } copy ||
            DecodeEntity(copy) != id || copy.AsString != id.ToString() ||
            value.Find("document") is not { } document || DecodeDocumentState(document) is not { } state ||
            state.Provenance != A.Provenance.HandAuthored) return null;
        bool valid = restoring ? state.Dirty && state.Token.DocumentId != token.DocumentId &&
            state.Token.DocumentRevision > token.DocumentRevision && state.Token.DocumentRevision - token.DocumentRevision == 1
            : state.Token == token;
        return valid ? new(id, state) : null;
    }

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
    public static string PrefabSource(string name) => "prefabs/prefab_" + name + "/source.scene.txt";

    public static A.PrefabRegistration? DecodePrefabRegistration(JsonValue value) =>
        Exact(value, 3) && value.Find("prefab") is { } prefab &&
        DecodeAsset(prefab) is AssetGuid guid && !guid.IsNil && value.Find("source") is { IsString: true } source &&
        value.Find("documentRevision") is { } revision && DecodeUInt64(revision) is ulong captured
            ? new A.PrefabRegistration(guid, source.AsString, captured) : null;
    public static bool ValidPrefabDigest(string value) =>
        value.Length == 64 && System.Linq.Enumerable.All(value, c => c is (>= '0' and <= '9') or (>= 'a' and <= 'f'));

    public static Atlantis.Gameplay.Assets.PrefabRevision? DecodePrefabRevision(JsonValue value, AssetGuid expected) =>
        Exact(value, 2) && value.Find("prefab") is { } prefab && DecodeAsset(prefab) == expected &&
        value.Find("revision") is { IsString: true } revision && ValidPrefabDigest(revision.AsString)
            ? new(expected, revision.AsString) : null;

    public static A.PrefabUpdate? DecodePrefabUpdate(JsonValue value) =>
        Exact(value, 4) && value.Find("registration") is { } reg && DecodePrefabRegistration(reg) is { } registration &&
        value.Find("previousRevision") is { IsString: true } old && ValidPrefabDigest(old.AsString) &&
        value.Find("revision") is { IsString: true } revision && ValidPrefabDigest(revision.AsString) &&
        value.Find("changed") is { Kind: JsonKind.Bool } changed && changed.AsBool == (old.AsString != revision.AsString)
            ? new(registration, old.AsString, revision.AsString, changed.AsBool) : null;

    public static JsonValue Encode(A.PrefabSubtree subtree)
    {
        var slots = JsonValue.NewArray();
        foreach (ulong slot in subtree.Slots) slots.Push(JsonValue.Number(slot));
        return JsonValue.NewObject().Set("source", JsonValue.String(subtree.Source))
            .Set("parent", EncodeOptional(subtree.Parent)).Set("slots", slots).Set("order", JsonValue.String(subtree.Order));
    }

    public static A.PrefabSubtree? DecodePrefabSubtree(JsonValue value)
    {
        (bool validParent, EntityGuid? parent) = OptionalEntityMember(value, "parent");
        if (!Exact(value, 4) || !validParent || value.Find("source") is not { IsString: true } source ||
            System.Text.Encoding.UTF8.GetByteCount(source.AsString) > 65536 ||
            value.Find("slots") is not { IsArray: true } slots || slots.Items.Count > 64 ||
            value.Find("order") is not { IsString: true } order || !ValidPrefabDigest(order.AsString) ||
            (source.AsString.Length == 0) != (slots.Items.Count == 0)) return null;
        var parsed = new List<ulong>(slots.Items.Count);
        foreach (JsonValue slot in slots.Items)
        {
            if (DecodeUInt64(slot) is not ulong index || (parsed.Count > 0 && parsed[^1] >= index)) return null;
            parsed.Add(index);
        }
        return new(source.AsString, parent, parsed.AsReadOnly(), order.AsString);
    }

    public static A.PrefabEdit? DecodePrefabEdit(JsonValue value) =>
        Exact(value, 5) && value.Find("document") is { } document && DecodeDocumentToken(document) is { } token &&
        value.Find("before") is { } before && DecodePrefabSubtree(before) is { } a &&
        value.Find("after") is { } after && DecodePrefabSubtree(after) is { } b &&
        value.Find("changed") is { Kind: JsonKind.Bool } changed && changed.AsBool != a.Matches(b) &&
        value.Find("affectsProjection") is { Kind: JsonKind.Bool } projection && (changed.AsBool || !projection.AsBool)
            ? new(token, a, b, changed.AsBool, projection.AsBool) : null;

    public static A.PrefabEditError? DecodePrefabEditError(JsonValue value) => DecodeName<A.PrefabEditError>(value);

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
