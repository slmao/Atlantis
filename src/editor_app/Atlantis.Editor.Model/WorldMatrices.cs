// Plan 0068 M1 (Spec 0068 R1, R5; ADR-0137 D1): world matrices over the
// authoring document's hierarchy, as the authoring stage's World solves them
// (src/world/src/world.cpp: updateTransforms(), getWorldMatrix(); the bake
// copies them bit for bit, Spec 0051): local = T * Ry(yaw) * Rx(pitch) *
// Rz(roll) * S (ADR-0050), world = parent's world * local. Column-major 16
// floats, multiplied element by element in the C++ order. Held to the C++
// World by tests/csharp/conformance/world_matrix_vectors.jsonl. UI-free.
using System;
using System.Collections.Generic;
using System.Numerics;

namespace Atlantis.Editor.Model;

/// <summary>A node's local transform: position, Euler angles (pitch x, yaw y, roll z; radians) and scale.</summary>
public readonly record struct LocalTransform(Vector3 Position, Vector3 EulerRadians, Vector3 Scale)
{
    public static readonly LocalTransform Identity = new(Vector3.Zero, Vector3.Zero, Vector3.One);
}

public static class WorldMatrices
{
    public static float[] Identity() => new float[] { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };

    /// <summary>local = T * R * S, R = Ry(yaw) * Rx(pitch) * Rz(roll).</summary>
    public static float[] ComposeLocal(LocalTransform t) =>
        Multiply(Translation(t.Position), Multiply(EulerRotation(t.EulerRadians), ScaleMatrix(t.Scale)));

    /// <summary>
    /// The world matrix of every node, given each node's parent (an index into the list, or null at the root) and its
    /// local transform. Parents may come in any order; a cycle throws.
    /// </summary>
    public static float[][] Solve(IReadOnlyList<(int? Parent, LocalTransform Local)> nodes)
    {
        var world = new float[nodes.Count][];
        var visiting = new bool[nodes.Count];
        float[] Of(int i)
        {
            if (world[i] is not null) return world[i];
            if (visiting[i]) throw new ArgumentException("the hierarchy has a cycle", nameof(nodes));
            visiting[i] = true;
            float[] local = ComposeLocal(nodes[i].Local);
            world[i] = nodes[i].Parent is int p ? Multiply(Of(p), local) : local;
            return world[i];
        }
        for (int i = 0; i < nodes.Count; ++i) Of(i);
        return world;
    }

    /// <summary>result = a * b, column-major, element by element as the C++ World sums.</summary>
    public static float[] Multiply(float[] a, float[] b)
    {
        var result = new float[16];
        for (int col = 0; col < 4; ++col)
        {
            for (int row = 0; row < 4; ++row)
            {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k) sum += a[k * 4 + row] * b[col * 4 + k];
                result[col * 4 + row] = sum;
            }
        }
        return result;
    }

    private static float[] Translation(Vector3 t)
    {
        float[] m = Identity();
        m[12] = t.X;
        m[13] = t.Y;
        m[14] = t.Z;
        return m;
    }

    private static float[] ScaleMatrix(Vector3 s)
    {
        float[] m = Identity();
        m[0] = s.X;
        m[5] = s.Y;
        m[10] = s.Z;
        return m;
    }

    private static float[] RotationX(float theta)
    {
        float c = MathF.Cos(theta), s = MathF.Sin(theta);
        return new[] { 1, 0, 0, 0, 0, c, s, 0, 0, -s, c, 0, 0, 0, 0, 1.0f };
    }

    private static float[] RotationY(float theta)
    {
        float c = MathF.Cos(theta), s = MathF.Sin(theta);
        return new[] { c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, 0, 0, 0, 1.0f };
    }

    private static float[] RotationZ(float theta)
    {
        float c = MathF.Cos(theta), s = MathF.Sin(theta);
        return new[] { c, s, 0, 0, -s, c, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1.0f };
    }

    private static float[] EulerRotation(Vector3 radians) =>
        Multiply(RotationY(radians.Y), Multiply(RotationX(radians.X), RotationZ(radians.Z)));
}
