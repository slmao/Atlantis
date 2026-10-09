// Spec 0058 R1/R6, ADR-0112 D2, Plan 0058 P6: the wire encoding of
// atlantis.remote/1's values (docs/architecture/remote_protocol.md section 6),
// mirroring the C++ codec (src/remote/src/codec.cpp) -- the reference:
// encoders build the same JSON, member for member, and decoders accept what
// it accepts and return null for anything malformed (the caller turns that
// into a protocol error). Pure functions.
using System;
using System.Collections.Generic;
using System.Numerics;

namespace Atlantis.Gameplay.Remote;

public static class Codec
{
    private const string NilGuid = "00000000-0000-0000-0000-000000000000";

    private static readonly (EventKind Kind, string Name)[] EventKinds =
    {
        (EventKind.EntityCreated, "EntityCreated"),
        (EventKind.EntityDestroyed, "EntityDestroyed"),
        (EventKind.ComponentAdded, "ComponentAdded"),
        (EventKind.ComponentRemoved, "ComponentRemoved"),
        (EventKind.PropertyChanged, "PropertyChanged"),
    };

    // --- Scalars and identifiers.

    public static JsonValue EncodeFloat(float value)
    {
        if (float.IsNaN(value)) return JsonValue.String("nan");
        if (float.IsInfinity(value)) return JsonValue.String(value > 0 ? "inf" : "-inf");
        return JsonValue.Number(value);
    }

    public static float? DecodeFloat(JsonValue value)
    {
        if (value.IsString)
        {
            return value.AsString switch
            {
                "nan" => float.NaN,
                "inf" => float.PositiveInfinity,
                "-inf" => float.NegativeInfinity,
                _ => null,
            };
        }
        return value.TryGetFloat(out float f) ? f : null;
    }

    public static JsonValue EncodeId(ulong id) =>
        JsonValue.String("0x" + id.ToString("x16", System.Globalization.CultureInfo.InvariantCulture));

    public static ulong? DecodeId(JsonValue value)
    {
        if (!value.IsString) return null;
        string text = value.AsString;
        if (text.Length != 18 || text[0] != '0' || text[1] != 'x') return null;
        ulong output = 0;
        for (int i = 2; i < text.Length; ++i)
        {
            char c = text[i];
            output <<= 4;
            if (c >= '0' && c <= '9') output |= (uint)(c - '0');
            else if (c >= 'a' && c <= 'f') output |= (uint)(c - 'a' + 10);
            else return null;
        }
        return output;
    }

    public static JsonValue Encode(TypeId id) => EncodeId(id.Value);
    public static TypeId? DecodeTypeId(JsonValue value) => DecodeId(value) is ulong id ? new TypeId(id) : null;
    public static JsonValue Encode(FieldId id) => EncodeId(id.Value);
    public static FieldId? DecodeFieldId(JsonValue value) => DecodeId(value) is ulong id ? new FieldId(id) : null;

    public static JsonValue Encode(EntityGuid entity) => JsonValue.String(entity.IsNil ? NilGuid : entity.ToString());

    public static EntityGuid? DecodeEntity(JsonValue value)
    {
        if (!value.IsString) return null;
        if (value.AsString == NilGuid) return default(EntityGuid);
        if (!EntityGuid.TryParse(value.AsString, out EntityGuid guid) || guid.IsNil) return null;
        return guid;
    }

    public static JsonValue Encode(AssetGuid asset) => JsonValue.String(asset.IsNil ? NilGuid : asset.ToString());

    public static AssetGuid? DecodeAsset(JsonValue value)
    {
        if (!value.IsString) return null;
        if (value.AsString == NilGuid) return default(AssetGuid);
        if (!AssetGuid.TryParse(value.AsString, out AssetGuid guid) || guid.IsNil) return null;
        return guid;
    }

    /// <summary>A JSON integer (tickets, ids, counts, frames).</summary>
    public static ulong? DecodeUInt64(JsonValue value) => value.TryGetUInt64(out ulong v) ? v : null;

    public static JsonValue Encode(AccessError error) => JsonValue.String(error.ToString());
    public static AccessError? DecodeAccessError(JsonValue value) => DecodeName<AccessError>(value);
    public static JsonValue Encode(ConnectionError error) => JsonValue.String(error.ToString());
    public static ConnectionError? DecodeConnectionError(JsonValue value) => DecodeName<ConnectionError>(value);
    public static JsonValue Encode(ControlError error) => JsonValue.String(error.ToString());
    public static ControlError? DecodeControlError(JsonValue value) => DecodeName<ControlError>(value);

    private static T? DecodeName<T>(JsonValue value) where T : struct, Enum
    {
        if (!value.IsString) return null;
        foreach (T candidate in Enum.GetValues<T>())
        {
            if (candidate.ToString() == value.AsString) return candidate;
        }
        return null;
    }

    // --- PropertyValue.

    public static JsonValue Encode(PropertyValue value)
    {
        JsonValue output = JsonValue.NewObject();
        switch (value.Kind)
        {
            case PropertyKind.UInt64:
                output.Set("u64", JsonValue.String(value.AsUInt64().ToString(System.Globalization.CultureInfo.InvariantCulture)));
                break;
            case PropertyKind.Float32: output.Set("f32", EncodeFloat(value.AsFloat32())); break;
            case PropertyKind.Vec3: output.Set("vec3", EncodeFloats(value.AsVec3())); break;
            case PropertyKind.Vec4: output.Set("vec4", EncodeFloats(value.AsVec4())); break;
            case PropertyKind.AssetGuid: output.Set("assetGuid", Encode(value.AsAsset())); break;
            case PropertyKind.EntityGuid: output.Set("entityGuid", Encode(value.AsEntity())); break;
            case PropertyKind.Enum: output.Set("enum", JsonValue.Number(value.AsEnum())); break;
            default: output.Set("absent", JsonValue.Bool(true)); break;
        }
        return output;
    }

    public static PropertyValue? DecodePropertyValue(JsonValue value)
    {
        if (!value.IsObject || value.Members.Count != 1) return null;
        (string tag, JsonValue payload) = (value.Members[0].Key, value.Members[0].Value);
        switch (tag)
        {
            case "u64":
                return payload.IsString && DecimalUInt64(payload.AsString) is ulong number ? PropertyValue.FromUInt64(number) : null;
            case "f32":
                return DecodeFloat(payload) is float f ? PropertyValue.FromFloat32(f) : null;
            case "vec3":
            {
                float[]? v = DecodeFloats(payload, 3);
                return v is null ? null : PropertyValue.FromVec3(new Vector3(v[0], v[1], v[2]));
            }
            case "vec4":
            {
                float[]? v = DecodeFloats(payload, 4);
                return v is null ? null : PropertyValue.FromVec4(new Vector4(v[0], v[1], v[2], v[3]));
            }
            case "assetGuid": return DecodeAsset(payload) is AssetGuid a ? PropertyValue.FromAsset(a) : null;
            case "entityGuid": return DecodeEntity(payload) is EntityGuid e ? PropertyValue.FromEntity(e) : null;
            case "enum": return payload.TryGetInt64(out long n) ? PropertyValue.FromEnum(n) : null;
            case "absent": return payload.IsBool && payload.AsBool ? PropertyValue.Absent : null;
            default: return null;
        }
    }

    // Decimal digits only (no sign, no space), in range.
    private static ulong? DecimalUInt64(string text)
    {
        if (text.Length == 0 || text.Length > 20) return null;
        ulong output = 0;
        foreach (char c in text)
        {
            if (c < '0' || c > '9') return null;
            ulong digit = (ulong)(c - '0');
            if (output > (ulong.MaxValue - digit) / 10) return null;
            output = output * 10 + digit;
        }
        return output;
    }

    private static JsonValue EncodeFloats(Vector3 v) =>
        JsonValue.NewArray().Push(EncodeFloat(v.X)).Push(EncodeFloat(v.Y)).Push(EncodeFloat(v.Z));

    private static JsonValue EncodeFloats(Vector4 v) =>
        JsonValue.NewArray().Push(EncodeFloat(v.X)).Push(EncodeFloat(v.Y)).Push(EncodeFloat(v.Z)).Push(EncodeFloat(v.W));

    private static JsonValue EncodeFloats(IReadOnlyList<float> values)
    {
        JsonValue output = JsonValue.NewArray();
        foreach (float x in values) output.Push(EncodeFloat(x));
        return output;
    }

    private static float[]? DecodeFloats(JsonValue value, int count)
    {
        if (!value.IsArray || value.Items.Count != count) return null;
        var output = new float[count];
        for (int i = 0; i < count; ++i)
        {
            if (DecodeFloat(value.Items[i]) is not float x) return null;
            output[i] = x;
        }
        return output;
    }

    private static Vector3? DecodeVector3(JsonValue? value) =>
        value is not null && DecodeFloats(value, 3) is float[] v ? new Vector3(v[0], v[1], v[2]) : null;

    // --- Address, commands, events.

    public static JsonValue Encode(PropertyAddress address) =>
        JsonValue.NewObject().Set("entity", Encode(address.Entity)).Set("component", Encode(address.Component))
            .Set("field", Encode(address.Field));

    public static PropertyAddress? DecodeAddress(JsonValue value)
    {
        JsonValue? entity = value.Find("entity");
        JsonValue? component = value.Find("component");
        JsonValue? field = value.Find("field");
        if (entity is null || component is null || field is null) return null;
        if (DecodeEntity(entity) is not EntityGuid e || DecodeTypeId(component) is not TypeId c ||
            DecodeFieldId(field) is not FieldId f)
        {
            return null;
        }
        return new PropertyAddress(e, c, f);
    }

    public static JsonValue Encode(Command command)
    {
        JsonValue output = JsonValue.NewObject();
        switch (command)
        {
            case CreateEntity c:
                output.Set("kind", JsonValue.String("CreateEntity")).Set("entity", Encode(c.Entity));
                break;
            case DestroyEntity c:
                output.Set("kind", JsonValue.String("DestroyEntity")).Set("entity", Encode(c.Entity));
                break;
            case AddComponent c:
                output.Set("kind", JsonValue.String("AddComponent")).Set("entity", Encode(c.Entity))
                    .Set("component", Encode(c.Component));
                break;
            case RemoveComponent c:
                output.Set("kind", JsonValue.String("RemoveComponent")).Set("entity", Encode(c.Entity))
                    .Set("component", Encode(c.Component));
                break;
            case SetProperty c:
                output.Set("kind", JsonValue.String("SetProperty")).Set("address", Encode(c.Address))
                    .Set("value", Encode(c.Value));
                break;
            default: throw new ArgumentException("an unrecognized command", nameof(command));
        }
        return output;
    }

    public static Command? DecodeCommand(JsonValue value)
    {
        switch (KindOf(value))
        {
            case "CreateEntity": return EntityOf(value) is EntityGuid e ? new CreateEntity(e) : null;
            case "DestroyEntity": return EntityOf(value) is EntityGuid e2 ? new DestroyEntity(e2) : null;
            case "AddComponent":
                return EntityComponentOf(value) is var (ae, ac) ? new AddComponent(ae, ac) : null;
            case "RemoveComponent":
                return EntityComponentOf(value) is var (re, rc) ? new RemoveComponent(re, rc) : null;
            case "SetProperty":
                return AddressValueOf(value) is var (address, v) ? new SetProperty(address, v) : null;
            default: return null;
        }
    }

    public static JsonValue Encode(Event evt)
    {
        JsonValue output = JsonValue.NewObject();
        switch (evt)
        {
            case EntityCreated e:
                output.Set("kind", JsonValue.String("EntityCreated")).Set("entity", Encode(e.Entity));
                break;
            case EntityDestroyed e:
                output.Set("kind", JsonValue.String("EntityDestroyed")).Set("entity", Encode(e.Entity));
                break;
            case ComponentAdded e:
                output.Set("kind", JsonValue.String("ComponentAdded")).Set("entity", Encode(e.Entity))
                    .Set("component", Encode(e.Component));
                break;
            case ComponentRemoved e:
                output.Set("kind", JsonValue.String("ComponentRemoved")).Set("entity", Encode(e.Entity))
                    .Set("component", Encode(e.Component));
                break;
            case PropertyChanged e:
                output.Set("kind", JsonValue.String("PropertyChanged")).Set("address", Encode(e.Address))
                    .Set("value", Encode(e.Value));
                break;
            default: throw new ArgumentException("an unrecognized event", nameof(evt));
        }
        return output;
    }

    public static Event? DecodeEvent(JsonValue value)
    {
        switch (KindOf(value))
        {
            case "EntityCreated": return EntityOf(value) is EntityGuid e ? new EntityCreated(e) : null;
            case "EntityDestroyed": return EntityOf(value) is EntityGuid e2 ? new EntityDestroyed(e2) : null;
            case "ComponentAdded":
                return EntityComponentOf(value) is var (ae, ac) ? new ComponentAdded(ae, ac) : null;
            case "ComponentRemoved":
                return EntityComponentOf(value) is var (re, rc) ? new ComponentRemoved(re, rc) : null;
            case "PropertyChanged":
                return AddressValueOf(value) is var (address, v) ? new PropertyChanged(address, v) : null;
            default: return null;
        }
    }

    private static string? KindOf(JsonValue value) => value.Find("kind") is { IsString: true } kind ? kind.AsString : null;

    private static EntityGuid? EntityOf(JsonValue value) => value.Find("entity") is JsonValue entity ? DecodeEntity(entity) : null;

    private static (EntityGuid, TypeId)? EntityComponentOf(JsonValue value)
    {
        if (EntityOf(value) is not EntityGuid entity) return null;
        if (value.Find("component") is not JsonValue component || DecodeTypeId(component) is not TypeId type) return null;
        return (entity, type);
    }

    private static (PropertyAddress, PropertyValue)? AddressValueOf(JsonValue value)
    {
        JsonValue? address = value.Find("address");
        JsonValue? v = value.Find("value");
        if (address is null || v is null) return null;
        if (DecodeAddress(address) is not PropertyAddress a || DecodePropertyValue(v) is not PropertyValue pv) return null;
        return (a, pv);
    }

    // --- Failures, tickets, filters.

    public static JsonValue Encode(CommandFailure failure) =>
        JsonValue.NewObject().Set("ticket", JsonValue.Number(failure.Ticket.Value)).Set("error", Encode(failure.Error));

    public static CommandFailure? DecodeFailure(JsonValue value)
    {
        JsonValue? ticket = value.Find("ticket");
        JsonValue? error = value.Find("error");
        if (ticket is null || error is null) return null;
        if (DecodeUInt64(ticket) is not ulong t || DecodeAccessError(error) is not AccessError e) return null;
        return new CommandFailure(new CommandTicket(t), e);
    }

    public static JsonValue Encode(TransactionTicket ticket) =>
        JsonValue.NewObject().Set("first", JsonValue.Number(ticket.First.Value)).Set("count", JsonValue.Number(ticket.Count));

    public static TransactionTicket? DecodeTransactionTicket(JsonValue value)
    {
        JsonValue? first = value.Find("first");
        JsonValue? count = value.Find("count");
        if (first is null || count is null) return null;
        if (DecodeUInt64(first) is not ulong f || DecodeUInt64(count) is not ulong c) return null;
        return new TransactionTicket(new CommandTicket(f), c);
    }

    public static JsonValue Encode(EventFilter filter)
    {
        JsonValue kinds = JsonValue.NewArray();
        foreach ((EventKind kind, string name) in EventKinds)
        {
            if ((filter.Kinds & kind) != 0) kinds.Push(JsonValue.String(name));
        }
        return JsonValue.NewObject().Set("kinds", kinds)
            .Set("entity", filter.Entity is EntityGuid e ? Encode(e) : JsonValue.Null())
            .Set("component", filter.Component is TypeId c ? Encode(c) : JsonValue.Null());
    }

    public static EventFilter? DecodeFilter(JsonValue value)
    {
        JsonValue? kinds = value.Find("kinds");
        JsonValue? entity = value.Find("entity");
        JsonValue? component = value.Find("component");
        if (kinds is null || !kinds.IsArray || entity is null || component is null) return null;
        EventKind set = EventKind.None;
        foreach (JsonValue name in kinds.Items)
        {
            if (!name.IsString) return null;
            bool known = false;
            foreach ((EventKind kind, string kindName) in EventKinds)
            {
                if (name.AsString == kindName)
                {
                    set |= kind;
                    known = true;
                }
            }
            if (!known) return null;
        }
        EntityGuid? e = null;
        if (!entity.IsNull)
        {
            if (DecodeEntity(entity) is not EntityGuid decoded) return null;
            e = decoded;
        }
        TypeId? c = null;
        if (!component.IsNull)
        {
            if (DecodeTypeId(component) is not TypeId decoded) return null;
            c = decoded;
        }
        return new EventFilter(set, e, c);
    }

    // --- The schema (section 6.7).

    public static JsonValue EncodeSchema(IReadOnlyList<SchemaType> types)
    {
        JsonValue output = JsonValue.NewArray();
        foreach (SchemaType type in types)
        {
            JsonValue fields = JsonValue.NewArray();
            foreach (SchemaField field in type.Fields)
            {
                fields.Push(JsonValue.NewObject()
                    .Set("id", Encode(field.Id))
                    .Set("name", JsonValue.String(field.Name))
                    .Set("kind", JsonValue.String(field.Kind.ToString()))
                    .Set("primitive", JsonValue.String(field.Primitive.ToString()))
                    .Set("type", Encode(field.Type))
                    .Set("flags", JsonValue.Number((ulong)field.Flags))
                    .Set("offset", JsonValue.Number(field.WireOffset)));
            }
            JsonValue constants = JsonValue.NewArray();
            foreach (SchemaConstant constant in type.Constants)
            {
                constants.Push(JsonValue.NewObject().Set("name", JsonValue.String(constant.Name))
                    .Set("value", JsonValue.Number(constant.Value)));
            }
            output.Push(JsonValue.NewObject()
                .Set("id", Encode(type.Id))
                .Set("name", JsonValue.String(type.Name))
                .Set("kind", JsonValue.String(type.Kind.ToString()))
                .Set("version", JsonValue.Number((ulong)type.Version))
                .Set("fields", fields)
                .Set("constants", constants));
        }
        return output;
    }

    public static List<SchemaType>? DecodeSchema(JsonValue value)
    {
        if (!value.IsArray) return null;
        var types = new List<SchemaType>();
        foreach (JsonValue t in value.Items)
        {
            JsonValue? id = t.Find("id");
            JsonValue? name = t.Find("name");
            JsonValue? kind = t.Find("kind");
            JsonValue? version = t.Find("version");
            JsonValue? fields = t.Find("fields");
            JsonValue? constants = t.Find("constants");
            if (id is null || name is null || !name.IsString || kind is null || version is null || fields is null ||
                !fields.IsArray || constants is null || !constants.IsArray)
            {
                return null;
            }
            if (DecodeTypeId(id) is not TypeId typeId || DecodeName<TypeKind>(kind) is not TypeKind typeKind ||
                DecodeUInt64(version) is not ulong typeVersion)
            {
                return null;
            }
            var fieldTable = new List<SchemaField>();
            foreach (JsonValue f in fields.Items)
            {
                JsonValue? fid = f.Find("id");
                JsonValue? fname = f.Find("name");
                JsonValue? fkind = f.Find("kind");
                JsonValue? fprimitive = f.Find("primitive");
                JsonValue? ftype = f.Find("type");
                JsonValue? fflags = f.Find("flags");
                JsonValue? foffset = f.Find("offset");
                if (fid is null || fname is null || !fname.IsString || fkind is null || fprimitive is null || ftype is null ||
                    fflags is null || foffset is null)
                {
                    return null;
                }
                if (DecodeFieldId(fid) is not FieldId fieldId || DecodeName<TypeKind>(fkind) is not TypeKind fieldKind ||
                    DecodeName<PrimitiveKind>(fprimitive) is not PrimitiveKind primitive ||
                    DecodeTypeId(ftype) is not TypeId fieldType || DecodeUInt64(fflags) is not ulong flags ||
                    DecodeUInt64(foffset) is not ulong offset)
                {
                    return null;
                }
                fieldTable.Add(new SchemaField(fieldId, fname.AsString, fieldKind, primitive, fieldType, (FieldFlags)flags)
                {
                    WireOffset = offset,
                });
            }
            var constantTable = new List<SchemaConstant>();
            foreach (JsonValue c in constants.Items)
            {
                JsonValue? cname = c.Find("name");
                JsonValue? cvalue = c.Find("value");
                if (cname is null || !cname.IsString || cvalue is null || !cvalue.TryGetInt64(out long number)) return null;
                constantTable.Add(new SchemaConstant(cname.AsString, number));
            }
            types.Add(new SchemaType(typeId, name.AsString, typeKind, (uint)typeVersion, fieldTable, constantTable));
        }
        return types;
    }

    // --- Control values (section 6.6).

    public static JsonValue Encode(RuntimeStatus status) =>
        JsonValue.NewObject().Set("paused", JsonValue.Bool(status.Paused)).Set("frame", JsonValue.Number(status.Frame))
            .Set("scene", Encode(status.Scene));

    public static RuntimeStatus? DecodeStatus(JsonValue value)
    {
        JsonValue? paused = value.Find("paused");
        JsonValue? frame = value.Find("frame");
        JsonValue? scene = value.Find("scene");
        if (paused is null || !paused.IsBool || frame is null || scene is null) return null;
        if (DecodeUInt64(frame) is not ulong f || DecodeAsset(scene) is not AssetGuid s) return null;
        return new RuntimeStatus(paused.AsBool, f, s);
    }

    public static JsonValue Encode(StepRequest request) =>
        JsonValue.NewObject().Set("frames", JsonValue.Number((ulong)request.Frames))
            .Set("image", request.ImagePath is string path ? JsonValue.String(path) : JsonValue.Null());

    public static StepRequest? DecodeStepRequest(JsonValue value)
    {
        JsonValue? frames = value.Find("frames");
        JsonValue? image = value.Find("image");
        if (frames is null || image is null || !(image.IsNull || image.IsString)) return null;
        if (DecodeUInt64(frames) is not ulong n || n > uint.MaxValue) return null;
        return new StepRequest((uint)n, image.IsString ? image.AsString : null);
    }

    public static JsonValue Encode(FrameData data)
    {
        JsonValue directional = JsonValue.NewArray();
        foreach (FrameDirectionalLight light in data.DirectionalLights)
        {
            directional.Push(JsonValue.NewObject().Set("direction", EncodeFloats(light.Direction))
                .Set("color", EncodeFloats(light.Color)).Set("intensity", EncodeFloat(light.Intensity)));
        }
        JsonValue points = JsonValue.NewArray();
        foreach (FramePointLight light in data.PointLights)
        {
            points.Push(JsonValue.NewObject().Set("position", EncodeFloats(light.Position))
                .Set("color", EncodeFloats(light.Color)).Set("intensity", EncodeFloat(light.Intensity))
                .Set("range", EncodeFloat(light.Range)));
        }
        return JsonValue.NewObject().Set("directionalLights", directional).Set("pointLights", points)
            .Set("view", EncodeFloats(data.View)).Set("projection", EncodeFloats(data.Projection))
            .Set("drawItemCount", JsonValue.Number(data.DrawItemCount));
    }

    public static FrameData? DecodeFrameData(JsonValue value)
    {
        JsonValue? directional = value.Find("directionalLights");
        JsonValue? points = value.Find("pointLights");
        JsonValue? drawItems = value.Find("drawItemCount");
        if (directional is null || !directional.IsArray || points is null || !points.IsArray || drawItems is null) return null;
        var directionalLights = new List<FrameDirectionalLight>();
        foreach (JsonValue l in directional.Items)
        {
            if (DecodeVector3(l.Find("direction")) is not Vector3 direction || DecodeVector3(l.Find("color")) is not Vector3 color ||
                l.Find("intensity") is not JsonValue i || DecodeFloat(i) is not float intensity)
            {
                return null;
            }
            directionalLights.Add(new FrameDirectionalLight(direction, color, intensity));
        }
        var pointLights = new List<FramePointLight>();
        foreach (JsonValue l in points.Items)
        {
            if (DecodeVector3(l.Find("position")) is not Vector3 position || DecodeVector3(l.Find("color")) is not Vector3 color ||
                l.Find("intensity") is not JsonValue i || DecodeFloat(i) is not float intensity ||
                l.Find("range") is not JsonValue r || DecodeFloat(r) is not float range)
            {
                return null;
            }
            pointLights.Add(new FramePointLight(position, color, intensity, range));
        }
        if (value.Find("view") is not JsonValue viewValue || DecodeFloats(viewValue, 16) is not float[] view ||
            value.Find("projection") is not JsonValue projectionValue ||
            DecodeFloats(projectionValue, 16) is not float[] projection || DecodeUInt64(drawItems) is not ulong count)
        {
            return null;
        }
        return new FrameData(directionalLights, pointLights, view, projection, count);
    }

    public static JsonValue Encode(FrameReport report)
    {
        JsonValue output = JsonValue.NewObject().Set("frame", JsonValue.Number(report.Frame))
            .Set("applied", JsonValue.Bool(report.Applied)).Set("data", Encode(report.Data));
        if (report.Image is CapturedImage image)
        {
            output.Set("image", JsonValue.NewObject().Set("path", JsonValue.String(image.Path))
                .Set("width", JsonValue.Number((ulong)image.Width)).Set("height", JsonValue.Number((ulong)image.Height)));
        }
        else
        {
            output.Set("image", JsonValue.Null());
        }
        return output;
    }

    public static FrameReport? DecodeFrameReport(JsonValue value)
    {
        JsonValue? frame = value.Find("frame");
        JsonValue? applied = value.Find("applied");
        JsonValue? data = value.Find("data");
        JsonValue? image = value.Find("image");
        if (frame is null || applied is null || !applied.IsBool || data is null || image is null) return null;
        if (DecodeUInt64(frame) is not ulong f || DecodeFrameData(data) is not FrameData d) return null;
        CapturedImage? captured = null;
        if (!image.IsNull)
        {
            JsonValue? path = image.Find("path");
            JsonValue? width = image.Find("width");
            JsonValue? height = image.Find("height");
            if (path is null || !path.IsString || width is null || height is null) return null;
            if (DecodeUInt64(width) is not ulong w || DecodeUInt64(height) is not ulong h || w > uint.MaxValue ||
                h > uint.MaxValue)
            {
                return null;
            }
            captured = new CapturedImage(path.AsString, (uint)w, (uint)h);
        }
        return new FrameReport(f, applied.AsBool, d, captured);
    }

    private static string SeverityName(DiagnosticSeverity severity) => severity switch
    {
        DiagnosticSeverity.Warning => "warning",
        DiagnosticSeverity.Error => "error",
        _ => "fatal",
    };

    public static JsonValue Encode(DiagnosticBatch batch)
    {
        JsonValue entries = JsonValue.NewArray();
        foreach (Diagnostic record in batch.Entries)
        {
            entries.Push(JsonValue.NewObject().Set("sequence", JsonValue.Number(record.Sequence))
                .Set("severity", JsonValue.String(SeverityName(record.Severity))).Set("message", JsonValue.String(record.Message)));
        }
        return JsonValue.NewObject().Set("entries", entries).Set("latest", JsonValue.Number(batch.Latest))
            .Set("dropped", JsonValue.Number(batch.Dropped));
    }

    public static DiagnosticBatch? DecodeDiagnosticBatch(JsonValue value)
    {
        JsonValue? entries = value.Find("entries");
        JsonValue? latest = value.Find("latest");
        JsonValue? dropped = value.Find("dropped");
        if (entries is null || !entries.IsArray || latest is null || dropped is null) return null;
        var records = new List<Diagnostic>();
        foreach (JsonValue r in entries.Items)
        {
            JsonValue? sequence = r.Find("sequence");
            JsonValue? severity = r.Find("severity");
            JsonValue? message = r.Find("message");
            if (sequence is null || severity is null || !severity.IsString || message is null || !message.IsString) return null;
            DiagnosticSeverity? level = null;
            foreach (DiagnosticSeverity candidate in Enum.GetValues<DiagnosticSeverity>())
            {
                if (severity.AsString == SeverityName(candidate)) level = candidate;
            }
            if (DecodeUInt64(sequence) is not ulong s || level is not DiagnosticSeverity l) return null;
            records.Add(new Diagnostic(s, l, message.AsString));
        }
        if (DecodeUInt64(latest) is not ulong lt || DecodeUInt64(dropped) is not ulong d) return null;
        return new DiagnosticBatch(records, lt, d);
    }

    // --- In-band results: {"ok":<value>} / {"err":"<name>"}.

    public static JsonValue Ok(JsonValue value) => JsonValue.NewObject().Set("ok", value);
    public static JsonValue Err(JsonValue name) => JsonValue.NewObject().Set("err", name);
}
