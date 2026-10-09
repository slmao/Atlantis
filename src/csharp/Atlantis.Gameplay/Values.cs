// Spec 0058 R1/R3, ADR-0112 D4, Plan 0058 P6: the value vocabulary of
// atlantis.remote/1 in C# -- identifiers, GUIDs and PropertyValue, the
// counterparts of the C++ SDK's (World's access types). Plain values;
// immutable and safe to share between threads.
using System;
using System.Numerics;

namespace Atlantis.Gameplay;

/// <summary>A schema type's id: the 64-bit hash of its qualified name.</summary>
public readonly record struct TypeId(ulong Value)
{
    /// <summary>"0x" and 16 lowercase hex digits, the wire form.</summary>
    public override string ToString() => "0x" + Value.ToString("x16", System.Globalization.CultureInfo.InvariantCulture);
}

/// <summary>A field's id: the 64-bit hash of its type's name and its own.</summary>
public readonly record struct FieldId(ulong Value)
{
    /// <summary>"0x" and 16 lowercase hex digits, the wire form.</summary>
    public override string ToString() => "0x" + Value.ToString("x16", System.Globalization.CultureInfo.InvariantCulture);
}

/// <summary>
/// RFC 9562 GUID text as Atlantis writes it: 8-4-4-4-12 lowercase hex digits.
/// Uppercase, braces and any other form are refused.
/// </summary>
internal static class GuidText
{
    public static bool TryParse(string? text, out Guid guid)
    {
        guid = Guid.Empty;
        if (text is null || text.Length != 36) return false;
        for (int i = 0; i < 36; ++i)
        {
            char c = text[i];
            bool hyphen = i == 8 || i == 13 || i == 18 || i == 23;
            if (hyphen ? c != '-' : !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
        }
        guid = Guid.ParseExact(text, "D");
        return true;
    }

    public static string Format(Guid guid) => guid.ToString("D");  // lowercase
}

/// <summary>An entity's stable GUID. The default value is the nil GUID.</summary>
public readonly record struct EntityGuid(Guid Value)
{
    public bool IsNil => Value == Guid.Empty;

    /// <summary>Lowercase RFC 9562 text; throws <see cref="FormatException"/> otherwise.</summary>
    public static EntityGuid Parse(string text) =>
        TryParse(text, out EntityGuid guid) ? guid : throw new FormatException($"not lowercase RFC 9562 GUID text: '{text}'");

    public static bool TryParse(string? text, out EntityGuid guid)
    {
        bool ok = GuidText.TryParse(text, out Guid value);
        guid = new EntityGuid(value);
        return ok;
    }

    public override string ToString() => GuidText.Format(Value);
}

/// <summary>An asset's GUID. The default value is the nil GUID.</summary>
public readonly record struct AssetGuid(Guid Value)
{
    public bool IsNil => Value == Guid.Empty;

    /// <summary>Lowercase RFC 9562 text; throws <see cref="FormatException"/> otherwise.</summary>
    public static AssetGuid Parse(string text) =>
        TryParse(text, out AssetGuid guid) ? guid : throw new FormatException($"not lowercase RFC 9562 GUID text: '{text}'");

    public static bool TryParse(string? text, out AssetGuid guid)
    {
        bool ok = GuidText.TryParse(text, out Guid value);
        guid = new AssetGuid(value);
        return ok;
    }

    public override string ToString() => GuidText.Format(Value);
}

/// <summary>Which alternative a <see cref="PropertyValue"/> holds -- one per wire tag.</summary>
public enum PropertyKind : byte
{
    UInt64,      // "u64"
    Float32,     // "f32"
    Vec3,        // "vec3"
    Vec4,        // "vec4"
    AssetGuid,   // "assetGuid"
    EntityGuid,  // "entityGuid"
    Enum,        // "enum" (the constant's int64 value)
    Absent,      // "absent": an Optional field without a value
}

/// <summary>
/// One property's value: exactly one alternative of the World's PropertyValue
/// (a tagged union). The default value is <c>UInt64 0</c>. Equality compares
/// the alternative and its payload, floats with <see cref="float.Equals(float)"/>
/// (NaN equals NaN).
/// </summary>
public readonly struct PropertyValue : IEquatable<PropertyValue>
{
    private readonly ulong _bits;    // UInt64, Enum (as two's complement)
    private readonly Vector4 _floats;  // Float32 (X), Vec3 (XYZ), Vec4
    private readonly Guid _guid;     // AssetGuid, EntityGuid

    private PropertyValue(PropertyKind kind, ulong bits, Vector4 floats, Guid guid)
    {
        Kind = kind;
        _bits = bits;
        _floats = floats;
        _guid = guid;
    }

    public PropertyKind Kind { get; }

    public static PropertyValue FromUInt64(ulong value) => new(PropertyKind.UInt64, value, default, default);
    public static PropertyValue FromFloat32(float value) => new(PropertyKind.Float32, 0, new Vector4(value, 0, 0, 0), default);
    public static PropertyValue FromVec3(Vector3 value) => new(PropertyKind.Vec3, 0, new Vector4(value, 0), default);
    public static PropertyValue FromVec4(Vector4 value) => new(PropertyKind.Vec4, 0, value, default);
    public static PropertyValue FromAsset(AssetGuid value) => new(PropertyKind.AssetGuid, 0, default, value.Value);
    public static PropertyValue FromEntity(EntityGuid value) => new(PropertyKind.EntityGuid, 0, default, value.Value);
    public static PropertyValue FromEnum(long value) => new(PropertyKind.Enum, unchecked((ulong)value), default, default);
    public static PropertyValue Absent { get; } = new(PropertyKind.Absent, 0, default, default);

    public static implicit operator PropertyValue(ulong value) => FromUInt64(value);
    public static implicit operator PropertyValue(float value) => FromFloat32(value);
    public static implicit operator PropertyValue(Vector3 value) => FromVec3(value);
    public static implicit operator PropertyValue(Vector4 value) => FromVec4(value);
    public static implicit operator PropertyValue(AssetGuid value) => FromAsset(value);
    public static implicit operator PropertyValue(EntityGuid value) => FromEntity(value);

    public bool TryGetUInt64(out ulong value) { value = _bits; return Kind == PropertyKind.UInt64; }
    public bool TryGetFloat32(out float value) { value = _floats.X; return Kind == PropertyKind.Float32; }
    public bool TryGetVec3(out Vector3 value) { value = new Vector3(_floats.X, _floats.Y, _floats.Z); return Kind == PropertyKind.Vec3; }
    public bool TryGetVec4(out Vector4 value) { value = _floats; return Kind == PropertyKind.Vec4; }
    public bool TryGetAsset(out AssetGuid value) { value = new AssetGuid(_guid); return Kind == PropertyKind.AssetGuid; }
    public bool TryGetEntity(out EntityGuid value) { value = new EntityGuid(_guid); return Kind == PropertyKind.EntityGuid; }
    public bool TryGetEnum(out long value) { value = unchecked((long)_bits); return Kind == PropertyKind.Enum; }
    public bool IsAbsent => Kind == PropertyKind.Absent;

    public ulong AsUInt64() => TryGetUInt64(out ulong v) ? v : throw WrongKind(PropertyKind.UInt64);
    public float AsFloat32() => TryGetFloat32(out float v) ? v : throw WrongKind(PropertyKind.Float32);
    public Vector3 AsVec3() => TryGetVec3(out Vector3 v) ? v : throw WrongKind(PropertyKind.Vec3);
    public Vector4 AsVec4() => TryGetVec4(out Vector4 v) ? v : throw WrongKind(PropertyKind.Vec4);
    public AssetGuid AsAsset() => TryGetAsset(out AssetGuid v) ? v : throw WrongKind(PropertyKind.AssetGuid);
    public EntityGuid AsEntity() => TryGetEntity(out EntityGuid v) ? v : throw WrongKind(PropertyKind.EntityGuid);
    public long AsEnum() => TryGetEnum(out long v) ? v : throw WrongKind(PropertyKind.Enum);

    private InvalidOperationException WrongKind(PropertyKind wanted) =>
        new($"the PropertyValue holds {Kind}, not {wanted}");

    public bool Equals(PropertyValue other)
    {
        if (Kind != other.Kind) return false;
        return Kind switch
        {
            PropertyKind.UInt64 or PropertyKind.Enum => _bits == other._bits,
            PropertyKind.Float32 => _floats.X.Equals(other._floats.X),
            PropertyKind.Vec3 => _floats.X.Equals(other._floats.X) && _floats.Y.Equals(other._floats.Y) && _floats.Z.Equals(other._floats.Z),
            PropertyKind.Vec4 => _floats.Equals(other._floats),
            PropertyKind.AssetGuid or PropertyKind.EntityGuid => _guid == other._guid,
            _ => true,
        };
    }

    public override bool Equals(object? obj) => obj is PropertyValue other && Equals(other);
    public override int GetHashCode() => HashCode.Combine(Kind, _bits, _floats, _guid);
    public static bool operator ==(PropertyValue a, PropertyValue b) => a.Equals(b);
    public static bool operator !=(PropertyValue a, PropertyValue b) => !a.Equals(b);

    /// <summary>The wire form, e.g. <c>{"f32":4}</c>.</summary>
    public override string ToString() => Remote.Json.Write(Remote.Codec.Encode(this));
}

/// <summary>One property of one entity: the component and the leaf field.</summary>
public readonly record struct PropertyAddress(EntityGuid Entity, TypeId Component, FieldId Field);
