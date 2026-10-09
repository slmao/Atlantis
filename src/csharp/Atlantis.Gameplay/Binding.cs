// Spec 0058 R2/R3, ADR-0112 D3, Plan 0058 P5/P6 (J6): the vocabulary the
// generated C# bindings (Generated/*.g.cs, written by
// atlantis_sdk_codegen --lang csharp) are made of -- what each generated type
// was generated from (TypeBinding), its typed field handles, and the per-leaf
// mapping of its value struct to PropertyValue. Static abstract interface
// members carry each component's binding, leaves and codec, so the typed
// layer needs no reflection. No byte offset appears here or in generated
// code: values cross by copy (ADR-0034).
//
// Thread safety: immutable data and pure functions only.
using System;
using System.Numerics;
using System.Runtime.CompilerServices;

namespace Atlantis.Gameplay;

/// <summary>One field of a generated type, as the schema described it at generation.</summary>
public sealed record FieldBinding(
    string Name,
    FieldId Id,
    TypeKind Kind,
    PrimitiveKind Primitive,  // meaningful when Kind is Primitive
    TypeId Type,              // the referenced struct or enum; 0 otherwise
    bool Optional,
    bool Editable);

public sealed record EnumConstantBinding(string Name, long Value);

/// <summary>
/// A generated type: its qualified name, id, kind, version, fields and
/// constants. <see cref="ZeroIsDeclared"/>: for an enum, whether value 0 is a
/// declared constant (a value-initialized member holds 0).
/// </summary>
public sealed record TypeBinding(
    string Name,
    TypeId Id,
    TypeKind Kind,
    uint Version,
    FieldBinding[] Fields,
    EnumConstantBinding[] Constants,
    bool ZeroIsDeclared);

/// <summary>
/// Implemented by every generated component type (a struct no other struct
/// nests). The members are static, so the SDK reaches them through the type
/// parameter alone.
/// </summary>
public interface IComponent<TSelf> where TSelf : struct, IComponent<TSelf>
{
    /// <summary>The type's own binding.</summary>
    static abstract TypeBinding Binding { get; }

    /// <summary>Every binding of the generated module, for the recursive compatibility check.</summary>
    static abstract TypeBinding[] Module { get; }

    /// <summary>The leaves, in the schema's leaf order (nested structs expanded in place).</summary>
    static abstract FieldId[] Leaves { get; }

    /// <summary>Whether every leaf is Editable.</summary>
    static abstract bool AllEditable { get; }

    /// <summary>Writes one value per leaf; <paramref name="output"/> has <see cref="Leaves"/>' length.</summary>
    static abstract void Write(in TSelf value, Span<PropertyValue> output);

    /// <summary>False when a value holds another alternative than its leaf's (a schema mismatch).</summary>
    static abstract bool TryRead(ReadOnlySpan<PropertyValue> input, out TSelf value);
}

/// <summary>
/// Implemented instead of <see cref="IComponent{TSelf}"/> by a component whose
/// every leaf is Editable: only those can be added with a value.
/// </summary>
public interface IEditableComponent<TSelf> : IComponent<TSelf> where TSelf : struct, IEditableComponent<TSelf>
{
}

/// <summary>A leaf's decoder: false when the value holds another alternative.</summary>
public delegate bool LeafDecoder<T>(in PropertyValue value, out T result);

/// <summary>
/// The leaf conversions generated code names: one PropertyValue alternative
/// per leaf C# type, a generated enum carried as its int64 value, and
/// nullable variants (Opt*) for Optional fields (null: Absent).
/// </summary>
public static class Leaf
{
    public static PropertyValue U64(ulong value) => PropertyValue.FromUInt64(value);
    public static PropertyValue F32(float value) => PropertyValue.FromFloat32(value);
    public static PropertyValue Vec3(Vector3 value) => PropertyValue.FromVec3(value);
    public static PropertyValue Vec4(Vector4 value) => PropertyValue.FromVec4(value);
    public static PropertyValue Asset(AssetGuid value) => PropertyValue.FromAsset(value);
    public static PropertyValue Entity(EntityGuid value) => PropertyValue.FromEntity(value);

    public static PropertyValue Enum<T>(T value) where T : struct, System.Enum => PropertyValue.FromEnum(EnumBits(value));

    public static bool TryU64(in PropertyValue value, out ulong result) => value.TryGetUInt64(out result);
    public static bool TryF32(in PropertyValue value, out float result) => value.TryGetFloat32(out result);
    public static bool TryVec3(in PropertyValue value, out Vector3 result) => value.TryGetVec3(out result);
    public static bool TryVec4(in PropertyValue value, out Vector4 result) => value.TryGetVec4(out result);
    public static bool TryAsset(in PropertyValue value, out AssetGuid result) => value.TryGetAsset(out result);
    public static bool TryEntity(in PropertyValue value, out EntityGuid result) => value.TryGetEntity(out result);

    public static bool TryEnum<T>(in PropertyValue value, out T result) where T : struct, System.Enum
    {
        bool ok = value.TryGetEnum(out long bits);
        result = FromBits<T>(bits);
        return ok;
    }

    public static PropertyValue OptU64(ulong? value) => value is ulong v ? U64(v) : PropertyValue.Absent;
    public static PropertyValue OptF32(float? value) => value is float v ? F32(v) : PropertyValue.Absent;
    public static PropertyValue OptVec3(Vector3? value) => value is Vector3 v ? Vec3(v) : PropertyValue.Absent;
    public static PropertyValue OptVec4(Vector4? value) => value is Vector4 v ? Vec4(v) : PropertyValue.Absent;
    public static PropertyValue OptAsset(AssetGuid? value) => value is AssetGuid v ? Asset(v) : PropertyValue.Absent;
    public static PropertyValue OptEntity(EntityGuid? value) => value is EntityGuid v ? Entity(v) : PropertyValue.Absent;
    public static PropertyValue OptEnum<T>(T? value) where T : struct, System.Enum => value is T v ? Enum(v) : PropertyValue.Absent;

    public static bool TryOptU64(in PropertyValue value, out ulong? result) => TryOpt(value, TryU64, out result);
    public static bool TryOptF32(in PropertyValue value, out float? result) => TryOpt(value, TryF32, out result);
    public static bool TryOptVec3(in PropertyValue value, out Vector3? result) => TryOpt(value, TryVec3, out result);
    public static bool TryOptVec4(in PropertyValue value, out Vector4? result) => TryOpt(value, TryVec4, out result);
    public static bool TryOptAsset(in PropertyValue value, out AssetGuid? result) => TryOpt(value, TryAsset, out result);
    public static bool TryOptEntity(in PropertyValue value, out EntityGuid? result) => TryOpt(value, TryEntity, out result);

    public static bool TryOptEnum<T>(in PropertyValue value, out T? result) where T : struct, System.Enum =>
        TryOpt(value, TryEnum, out result);

    private static bool TryOpt<T>(in PropertyValue value, LeafDecoder<T> decode, out T? result) where T : struct
    {
        result = null;
        if (value.IsAbsent) return true;
        if (!decode(value, out T inner)) return false;
        result = inner;
        return true;
    }

    // Generated enums are long-based (": long"); the value's bits are its int64.
    private static long EnumBits<T>(T value) where T : struct, System.Enum
    {
        if (Unsafe.SizeOf<T>() != sizeof(long)) throw new NotSupportedException(typeof(T).Name + " is not a long-based enum");
        return Unsafe.As<T, long>(ref value);
    }

    private static T FromBits<T>(long bits) where T : struct, System.Enum
    {
        if (Unsafe.SizeOf<T>() != sizeof(long)) throw new NotSupportedException(typeof(T).Name + " is not a long-based enum");
        return Unsafe.As<long, T>(ref bits);
    }
}

/// <summary>
/// A typed handle on one leaf of component <typeparamref name="TC"/>: the
/// component's TypeId, the leaf's FieldId, its canonical path, and its value
/// conversion. Generated code declares one per leaf, as <see cref="Field{TC,TV}"/>
/// (Editable) or <see cref="ReadOnlyField{TC,TV}"/>.
/// </summary>
public abstract class FieldHandle<TC, TV> where TC : struct, IComponent<TC>
{
    private readonly Func<TV, PropertyValue> _encode;
    private readonly LeafDecoder<TV> _decode;

    private protected FieldHandle(TypeId component, FieldId field, string path, Func<TV, PropertyValue> encode,
                                  LeafDecoder<TV> decode)
    {
        Component = component;
        Id = field;
        Path = path;
        _encode = encode;
        _decode = decode;
    }

    public TypeId Component { get; }
    public FieldId Id { get; }

    /// <summary>The canonical path, e.g. "Camera.fog.density".</summary>
    public string Path { get; }

    public PropertyValue Encode(TV value) => _encode(value);
    public bool TryDecode(in PropertyValue value, out TV result) => _decode(value, out result);

    public override string ToString() => Path;
}

/// <summary>An Editable leaf: it can be read and set.</summary>
public sealed class Field<TC, TV> : FieldHandle<TC, TV> where TC : struct, IComponent<TC>
{
    public Field(TypeId component, FieldId field, string path, Func<TV, PropertyValue> encode, LeafDecoder<TV> decode)
        : base(component, field, path, encode, decode)
    {
    }
}

/// <summary>A leaf without the Editable flag: it can be read, and no typed set accepts it.</summary>
public sealed class ReadOnlyField<TC, TV> : FieldHandle<TC, TV> where TC : struct, IComponent<TC>
{
    public ReadOnlyField(TypeId component, FieldId field, string path, Func<TV, PropertyValue> encode, LeafDecoder<TV> decode)
        : base(component, field, path, encode, decode)
    {
    }
}
