// Spec 0058 R3, Plan 0058 P6: type names and property paths over a schema --
// the grammar of the C++ connection::text (findType, parsePath, leavesOf;
// Spec 0054 R6, Spec 0049 J8 addressing), ported so the C# SDK resolves names
// exactly as the C++ SDK does. Pure functions.
using System.Collections.Generic;

namespace Atlantis.Gameplay;

/// <summary>Why a name or path did not resolve.</summary>
public enum NameError
{
    UnknownType,   // no schema type has that short or qualified name
    UnknownField,  // a path segment names no field of the type it is in
    NotALeaf,      // the path stops at a struct, or names only a type
}

/// <summary>One leaf of a type, with its canonical path ("Camera.fog.density").</summary>
public sealed record LeafPath(string Path, SchemaField Leaf);

public static class SchemaText
{
    /// <summary>The name after the last "::".</summary>
    public static string ShortName(string qualifiedName)
    {
        int separator = qualifiedName.LastIndexOf("::", System.StringComparison.Ordinal);
        return separator < 0 ? qualifiedName : qualifiedName.Substring(separator + 2);
    }

    /// <summary>The type with that short or qualified name (the first, in schema order), or null.</summary>
    public static SchemaType? FindType(IReadOnlyList<SchemaType> schema, string name)
    {
        foreach (SchemaType type in schema)
        {
            if (type.Name == name || ShortName(type.Name) == name) return type;
        }
        return null;
    }

    public static SchemaType? FindType(IReadOnlyList<SchemaType> schema, TypeId id)
    {
        foreach (SchemaType type in schema)
        {
            if (type.Id == id) return type;
        }
        return null;
    }

    /// <summary>
    /// Resolves &lt;Type&gt;.&lt;field&gt;[.&lt;field&gt;...] to its component and leaf; on
    /// failure returns false with <paramref name="error"/> set.
    /// </summary>
    public static bool TryParsePath(IReadOnlyList<SchemaType> schema, string text, out TypeId component,
                                    out SchemaField? leaf, out NameError error)
    {
        component = default;
        leaf = null;
        error = NameError.UnknownType;
        int dot = text.IndexOf('.');
        SchemaType? type = FindType(schema, dot < 0 ? text : text.Substring(0, dot));
        if (type is null || type.Kind != TypeKind.Struct) return Fail(NameError.UnknownType, out error);
        if (dot < 0) return Fail(NameError.NotALeaf, out error);
        TypeId found = type.Id;
        string rest = text.Substring(dot + 1);
        while (true)
        {
            int next = rest.IndexOf('.');
            string name = next < 0 ? rest : rest.Substring(0, next);
            SchemaField? field = null;
            foreach (SchemaField candidate in type.Fields)
            {
                if (candidate.Name == name) field = candidate;
            }
            if (field is null) return Fail(NameError.UnknownField, out error);
            if (field.Kind != TypeKind.Struct)
            {
                if (next >= 0) return Fail(NameError.UnknownField, out error);  // a leaf has no fields
                component = found;
                leaf = field;
                return true;
            }
            if (next < 0) return Fail(NameError.NotALeaf, out error);
            type = FindType(schema, field.Type);
            if (type is null) return Fail(NameError.UnknownType, out error);
            rest = rest.Substring(next + 1);
        }
    }

    /// <summary>Every leaf of a type, in descriptor order (nested structs expanded in place).</summary>
    public static List<LeafPath> LeavesOf(IReadOnlyList<SchemaType> schema, TypeId type)
    {
        var leaves = new List<LeafPath>();
        SchemaType? descriptor = FindType(schema, type);
        if (descriptor is not null) Collect(schema, descriptor, ShortName(descriptor.Name), leaves);
        return leaves;
    }

    private static void Collect(IReadOnlyList<SchemaType> schema, SchemaType type, string prefix, List<LeafPath> output)
    {
        foreach (SchemaField field in type.Fields)
        {
            string path = prefix + "." + field.Name;
            if (field.Kind == TypeKind.Struct)
            {
                SchemaType nested = FindType(schema, field.Type)
                    ?? throw new SchemaMismatchException(path);  // a struct field's type is not in the schema
                Collect(schema, nested, path, output);
            }
            else
            {
                output.Add(new LeafPath(path, field));
            }
        }
    }

    private static bool Fail(NameError value, out NameError error)
    {
        error = value;
        return false;
    }
}
