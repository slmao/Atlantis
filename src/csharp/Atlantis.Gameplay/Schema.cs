// Spec 0058 R1/R4, Plan 0058 P6: a Runtime's schema as connection.schema
// returns it (remote_protocol.md section 6.7). The byte offset the wire
// carries is internal layout (ADR-0034): it is kept only so the codec
// re-encodes a schema to the same bytes, and nothing reads it.
using System.Collections.Generic;

namespace Atlantis.Gameplay;

public enum TypeKind
{
    Primitive,
    Struct,
    Enum,
}

public enum PrimitiveKind
{
    UInt64,
    Float32,
    Vec3Float32,
    Vec4Float32,
    AssetGuid,
    EntityGuid,
}

[System.Flags]
public enum FieldFlags : ulong
{
    None = 0,
    Serializable = 1,
    Editable = 2,
    AssetReference = 4,
    EntityReference = 8,
    Optional = 16,
}

public sealed record SchemaField(FieldId Id, string Name, TypeKind Kind, PrimitiveKind Primitive, TypeId Type, FieldFlags Flags)
{
    /// <summary>The wire's "offset": internal layout, never interpreted.</summary>
    internal ulong WireOffset { get; init; }

    public bool IsEditable => (Flags & FieldFlags.Editable) != 0;
    public bool IsOptional => (Flags & FieldFlags.Optional) != 0;
}

public sealed record SchemaConstant(string Name, long Value);

public sealed record SchemaType(
    TypeId Id,
    string Name,
    TypeKind Kind,
    uint Version,
    IReadOnlyList<SchemaField> Fields,
    IReadOnlyList<SchemaConstant> Constants)
{
    /// <summary>The name after the last "::" (Light for world::Light).</summary>
    public string ShortName => SchemaText.ShortName(Name);
}
