// Plan 0058 P9 (Spec 0058 R2, R3; ADR-0112 D3): the "generated" suite -- the
// committed bindings. World.g.cs's bindings equal the schema the fixture
// server serves (the World's own); every id in both generated files is the
// FNV-1a-64 hash of its name (the check the C++ header static_asserts);
// handles are PascalCase with canonical paths, read-only leaves get
// ReadOnlyField; value initialization is zeros, nulls and enum value 0,
// declared only where ZeroIsDeclared says.
using System;
using System.Collections.Generic;
using System.Numerics;
using System.Text;
using S = Atlantis.Gameplay.Synthetic;
using W = Atlantis.Gameplay.World;

namespace Atlantis.Gameplay.Tests;

public static class GeneratedSuite
{
    public static void Run(TestContext t)
    {
        using (var server = new FixtureServer(t))
        using (Remote.RemoteSession session = server.Attach())
        {
            t.Equal(session.Schema.Count, W.Bindings.Types.Length, "World.g.cs binds every type of the served schema");
            foreach (TypeBinding binding in W.Bindings.Types) t.Check(SameAsSchema(binding, session.Schema), binding.Name + ": as served");
        }
        IdsAreHashes(t, W.Bindings.Types, "World.g.cs");
        IdsAreHashes(t, S.Bindings.Types, "Synthetic.g.cs");

        // Handles.
        t.Equal("Light.intensity", W.Fields.Light.Intensity.Path, "Fields.Light.Intensity is Light.intensity");
        t.Equal("Camera.fog.density", W.Fields.Camera.Fog.Density.Path, "Fields.Camera.Fog.Density is Camera.fog.density");
        t.Equal(Fnv(Encoding.UTF8.GetBytes("world::Camera")), W.Fields.Camera.Fog.Density.Component.Value,
                "a nested leaf's handle names its component");
        t.Equal(Fnv(Encoding.UTF8.GetBytes("world::CameraFog.density")), W.Fields.Camera.Fog.Density.Id.Value,
                "... and its own field id");
        t.Check(S.Fields.Locked.Fixed is ReadOnlyField<S.Locked, float>, "a read-only leaf gets ReadOnlyField");
        t.Check(S.Fields.Locked.Free is Field<S.Locked, float>, "an editable leaf gets Field");
        t.Check(!IsEditable<S.Locked>() && IsEditable<S.Probe>() && IsEditable<W.Light>(), "IEditableComponent only on all-Editable types");
        t.Equal(new[] { W.Fields.Light.Kind.Id, W.Fields.Light.Color.Id, W.Fields.Light.Intensity.Id, W.Fields.Light.Range.Id },
                LeavesOf<W.Light>(), "Light's leaves in schema order", SequenceComparer<FieldId>.Instance);
        t.Equal(11, LeavesOf<W.Camera>().Length, "Camera's leaves: nested structs expanded in place");

        // Value initialization (Spec 0057 R10 over C#).
        W.Light light = default;
        t.Check(light.Kind == W.LightKind.Directional && light.Color == Vector3.Zero && light.Intensity == 0 && light.Range == 0,
                "default(Light): zeros and enum value 0 (Directional)");
        t.Check(default(W.Renderable).MaterialAsset is null && default(W.Renderable).MeshAsset == 0,
                "default(Renderable): an Optional leaf is null");
        t.Check(BindingOf("world::LightKind", W.Bindings.Types).ZeroIsDeclared, "LightKind: 0 is declared (Directional)");
        t.Check(!BindingOf("synthetic::Mode", S.Bindings.Types).ZeroIsDeclared && (long)default(S.Mode) == 0 &&
                !Enum.IsDefined(default(S.Mode)), "Mode: 0 is not declared");
        t.Check(BindingOf("synthetic::Phase", S.Bindings.Types).ZeroIsDeclared && default(S.Phase) == S.Phase.Zero,
                "Phase: 0 is declared (Zero)");
        t.Equal(-2L, (long)S.Mode.B, "enumerators carry their exact values");

        // Write and TryRead are inverse, leaf by leaf.
        var beacon = new W.Light { Kind = W.LightKind.Point, Color = new Vector3(1, 0.6f, 0.2f), Intensity = 4, Range = 4 };
        PropertyValue[] values = WriteAll(beacon);
        t.Check(values.Length == 4 && values[0] == PropertyValue.FromEnum(1) && values[2] == PropertyValue.FromFloat32(4),
                "Write: one PropertyValue per leaf, enums as their int64");
        t.Check(ReadAll<W.Light>(values, out W.Light back) && back == beacon, "TryRead(Write(v)) == v");
        values[2] = PropertyValue.FromUInt64(4);
        t.Check(!ReadAll<W.Light>(values, out _), "TryRead refuses another alternative");
        var renderable = new W.Renderable { MeshAsset = 7, MaterialAsset = null };
        PropertyValue[] optional = WriteAll(renderable);
        t.Check(optional[1].IsAbsent && ReadAll<W.Renderable>(optional, out W.Renderable r) && r == renderable,
                "an Optional null is Absent, and back");
    }

    private static bool SameAsSchema(TypeBinding binding, IReadOnlyList<SchemaType> schema)
    {
        SchemaType? type = SchemaText.FindType(schema, binding.Id);
        if (type is null || type.Name != binding.Name || type.Kind != binding.Kind || type.Version != binding.Version) return false;
        if (type.Fields.Count != binding.Fields.Length || type.Constants.Count != binding.Constants.Length) return false;
        for (int i = 0; i < binding.Fields.Length; ++i)
        {
            FieldBinding f = binding.Fields[i];
            SchemaField d = type.Fields[i];
            if (f.Name != d.Name || f.Id != d.Id || f.Kind != d.Kind || f.Type != d.Type || f.Optional != d.IsOptional ||
                f.Editable != d.IsEditable || (f.Kind == TypeKind.Primitive && f.Primitive != d.Primitive))
            {
                return false;
            }
        }
        for (int i = 0; i < binding.Constants.Length; ++i)
        {
            if (binding.Constants[i].Name != type.Constants[i].Name || binding.Constants[i].Value != type.Constants[i].Value) return false;
        }
        bool zero = false;
        foreach (SchemaConstant constant in type.Constants) zero |= constant.Value == 0;
        return zero == binding.ZeroIsDeclared;
    }

    private static void IdsAreHashes(TestContext t, TypeBinding[] module, string what)
    {
        bool all = true;
        foreach (TypeBinding type in module)
        {
            all &= type.Id.Value == Fnv(Encoding.UTF8.GetBytes(type.Name));
            foreach (FieldBinding field in type.Fields) all &= field.Id.Value == Fnv(Encoding.UTF8.GetBytes(type.Name + "." + field.Name));
        }
        t.Check(all, what + ": every TypeId and FieldId is the FNV-1a-64 of its name");
    }

    private static ulong Fnv(byte[] text)
    {
        ulong hash = 0xcbf29ce484222325UL;
        foreach (byte b in text)
        {
            hash ^= b;
            hash *= 1099511628211UL;
        }
        return hash;
    }

    private static TypeBinding BindingOf(string name, TypeBinding[] module) => Array.Find(module, b => b.Name == name)!;

    private static bool IsEditable<T>() where T : struct, IComponent<T> =>
        T.AllEditable && Array.Exists(typeof(T).GetInterfaces(),
                                      i => i.IsGenericType && i.GetGenericTypeDefinition() == typeof(IEditableComponent<>));

    private static FieldId[] LeavesOf<T>() where T : struct, IComponent<T> => T.Leaves;

    private static PropertyValue[] WriteAll<T>(T value) where T : struct, IComponent<T>
    {
        var output = new PropertyValue[T.Leaves.Length];
        T.Write(value, output);
        return output;
    }

    private static bool ReadAll<T>(PropertyValue[] input, out T value) where T : struct, IComponent<T> => T.TryRead(input, out value);
}

public sealed class SequenceComparer<T> : IEqualityComparer<T[]>
{
    public static SequenceComparer<T> Instance { get; } = new();

    public bool Equals(T[]? a, T[]? b) => a is not null && b is not null && System.Linq.Enumerable.SequenceEqual(a, b);

    public int GetHashCode(T[] obj) => obj.Length;
}
