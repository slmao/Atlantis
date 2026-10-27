// Plan 0068 M1 (Spec 0068 R1, R5; ADR-0137 D1, D3): E1's Gizmo math
// (src/editor/include/atlantis/editor/model/gizmo.h), ported to C# --
// decompose/compose (M = T * R * S), the axes of each mode and space,
// snapping, the matrix a drag gives, and the screen handles with their grab
// and drag amount (moved out of the ImGui view into gizmo.h by ADR-0137 D3).
// Matrices are 16 floats, column-major (column k is m[k*4 .. k*4+3]). Held to
// the C++ functions by tests/csharp/conformance/gizmo_vectors.jsonl. UI-free.
using System;
using System.Numerics;

namespace Atlantis.Editor.Model;

public enum GizmoMode
{
    Translate,
    Rotate,
    Scale,
}

public enum GizmoAxis
{
    X,
    Y,
    Z,
}

/// <summary>The axes Translate and Rotate use: the world's, or the entity's own. Scale always uses the entity's own.</summary>
public enum GizmoSpace
{
    World,
    Local,
}

/// <summary>
/// Translation, then a proper rotation (column-major 3x3, orthonormal, determinant +1), then a per-axis scale (a
/// reflection is carried as a negative x scale): M = T * R * S.
/// </summary>
public sealed record Trs(float[] Translation, float[] Rotation, float[] Scale)
{
    public static Trs Identity() => new(new float[3], new float[] { 1, 0, 0, 0, 1, 0, 0, 0, 1 }, new float[] { 1, 1, 1 });
}

/// <summary>One axis handle in pixels; <c>PixelsPerUnit</c> is how many pixels one world unit along the axis covers.</summary>
public readonly record struct GizmoHandle(Vector2 Origin, Vector2 Direction, Vector2 End, float PixelsPerUnit);

/// <summary>A grabbed handle: its axis, where it was grabbed, the handle, the pointer's angle then, and whether the axis faced the camera.</summary>
public readonly record struct GizmoGrab(GizmoAxis Axis, Vector2 Start, GizmoHandle Handle, float StartAngle, bool TowardCamera);

public static class Gizmo
{
    /// <summary>Two normalized basis columns whose |dot| exceeds this are sheared.</summary>
    public const float ShearTolerance = 1e-4f;
    public const float TranslateSnap = 0.5f;           // world units
    public const float RotateSnapRadians = 0.2617994f; // 15 degrees
    public const float ScaleSnap = 0.1f;               // of the factor
    public const float HandlePixels = 80.0f;
    public const float PickPixels = 8.0f;
    public const float RotateRingPixels = 9.0f;

    /// <summary>Null when the matrix has no TRS form: sheared, a degenerate axis, or a bottom row other than (0, 0, 0, 1).</summary>
    public static Trs? Decompose(float[] m)
    {
        if (m[3] != 0.0f || m[7] != 0.0f || m[11] != 0.0f || m[15] != 1.0f) return null;
        var translation = new[] { m[12], m[13], m[14] };
        var scale = new float[3];
        var axes = new float[3][];
        for (int k = 0; k < 3; ++k)
        {
            float x = m[k * 4], y = m[k * 4 + 1], z = m[k * 4 + 2];
            float length = MathF.Sqrt(x * x + y * y + z * z);
            if (!(length > 1e-8f) || !float.IsFinite(length)) return null;
            scale[k] = length;
            axes[k] = new[] { x / length, y / length, z / length };
        }
        if (MathF.Abs(Dot(axes[0], axes[1])) > ShearTolerance || MathF.Abs(Dot(axes[0], axes[2])) > ShearTolerance ||
            MathF.Abs(Dot(axes[1], axes[2])) > ShearTolerance)
        {
            return null;
        }
        if (Dot(axes[0], Cross(axes[1], axes[2])) < 0.0f)  // a reflection: carried by the x scale
        {
            scale[0] = -scale[0];
            axes[0] = new[] { -axes[0][0], -axes[0][1], -axes[0][2] };
        }
        var rotation = new float[9];
        for (int k = 0; k < 3; ++k)
        {
            for (int i = 0; i < 3; ++i) rotation[k * 3 + i] = axes[k][i];
        }
        return new Trs(translation, rotation, scale);
    }

    public static float[] Compose(Trs trs)
    {
        var m = new float[16];
        for (int k = 0; k < 3; ++k)
        {
            for (int i = 0; i < 3; ++i) m[k * 4 + i] = trs.Rotation[k * 3 + i] * trs.Scale[k];
            m[k * 4 + 3] = 0.0f;
        }
        m[12] = trs.Translation[0];
        m[13] = trs.Translation[1];
        m[14] = trs.Translation[2];
        m[15] = 1.0f;
        return m;
    }

    /// <summary>
    /// The world-space unit direction of <paramref name="axis"/>: a world axis (Translate, Rotate in World), or the
    /// entity's own axis from <paramref name="trs"/> (flipped for the x axis of a reflection).
    /// </summary>
    public static Vector3 AxisDirection(Trs trs, GizmoMode mode, GizmoSpace space, GizmoAxis axis)
    {
        int k = (int)axis;
        bool local = mode == GizmoMode.Scale || space == GizmoSpace.Local;
        if (!local) return k == 0 ? Vector3.UnitX : k == 1 ? Vector3.UnitY : Vector3.UnitZ;
        float sign = k == 0 && trs.Scale[0] < 0.0f ? -1.0f : 1.0f;
        return new Vector3(trs.Rotation[k * 3 + 0] * sign, trs.Rotation[k * 3 + 1] * sign, trs.Rotation[k * 3 + 2] * sign);
    }

    /// <summary>A drag amount rounded to its mode's step (a scale factor no smaller than one step).</summary>
    public static float SnapAmount(GizmoMode mode, float amount) => mode switch
    {
        GizmoMode.Translate => Round(amount / TranslateSnap) * TranslateSnap,
        GizmoMode.Rotate => Round(amount / RotateSnapRadians) * RotateSnapRadians,
        _ => MathF.Max(Round(amount / ScaleSnap) * ScaleSnap, ScaleSnap),
    };

    /// <summary>The matrix a drag of <paramref name="amount"/> along <paramref name="axis"/> gives from <paramref name="start"/>.</summary>
    public static float[] DragMatrix(float[] start, GizmoMode mode, GizmoSpace space, GizmoAxis axis, float amount, bool snap)
    {
        Trs startTrs = Decompose(start) ?? throw new ArgumentException("the start matrix has no TRS form", nameof(start));
        int k = (int)axis;
        if (snap) amount = SnapAmount(mode, amount);
        switch (mode)
        {
            case GizmoMode.Translate:
            {
                var columns = (float[])start.Clone();
                if (space == GizmoSpace.World)
                {
                    columns[12 + k] = start[12 + k] + amount;
                }
                else
                {
                    Vector3 direction = AxisDirection(startTrs, mode, space, axis);
                    columns[12] = start[12] + direction.X * amount;
                    columns[13] = start[13] + direction.Y * amount;
                    columns[14] = start[14] + direction.Z * amount;
                }
                return columns;
            }
            case GizmoMode.Rotate:
            {
                // World: about the world axis (R_axis * R). Local: about the entity's own axis (R * R_axis).
                float[] rotation = space == GizmoSpace.World
                    ? Multiply3(AxisRotation(axis, amount), startTrs.Rotation)
                    : Multiply3(startTrs.Rotation, AxisRotation(axis, amount));
                return Compose(startTrs with { Rotation = rotation });
            }
            default:
            {
                var scale = (float[])startTrs.Scale.Clone();
                scale[k] = startTrs.Scale[k] * MathF.Max(amount, 1e-4f);
                return Compose(startTrs with { Scale = scale });
            }
        }
    }

    /// <summary>
    /// The handles of a Gizmo at <paramref name="position"/> with its axes' world <paramref name="directions"/>; null
    /// when the position is behind the camera. An axis whose tip is behind, or within 1e-3 pixel of the origin, has none.
    /// </summary>
    public static GizmoHandle?[]? Handles(CameraMatrices camera, PixelRect rect, Vector3 position, Vector3[] directions)
    {
        if (Picking.PixelOf(camera, rect, position) is not Vector2 center) return null;
        var handles = new GizmoHandle?[3];
        for (int axis = 0; axis < 3; ++axis)
        {
            Vector3 d = directions[axis];
            var unit = new Vector3(position.X + d.X, position.Y + d.Y, position.Z + d.Z);
            if (Picking.PixelOf(camera, rect, unit) is not Vector2 tip) continue;
            float dx = tip.X - center.X, dy = tip.Y - center.Y;
            float pixels = MathF.Sqrt(dx * dx + dy * dy);
            if (pixels < 1e-3f) continue;
            var direction = new Vector2(dx / pixels, dy / pixels);
            handles[axis] = new GizmoHandle(center, direction,
                                            new Vector2(center.X + direction.X * HandlePixels, center.Y + direction.Y * HandlePixels),
                                            pixels);
        }
        return handles;
    }

    /// <summary>Whether a world direction points toward the camera (view space looks down -z).</summary>
    public static bool AxisFacesCamera(CameraMatrices camera, Vector3 direction) =>
        camera.View[2] * direction.X + camera.View[6] * direction.Y + camera.View[10] * direction.Z > 0.0f;

    /// <summary>The handle a press grabs: the nearest within <see cref="PickPixels"/> (a later axis wins a tie), or none.</summary>
    public static GizmoGrab? Grab(GizmoHandle?[] handles, Vector3[] directions, CameraMatrices camera, GizmoMode mode, Vector2 pointer)
    {
        GizmoGrab? grab = null;
        float best = PickPixels;
        for (int axis = 0; axis < 3; ++axis)
        {
            if (handles[axis] is not GizmoHandle handle) continue;
            float distance = mode == GizmoMode.Rotate
                ? MathF.Abs(PixelLength(pointer.X - handle.End.X, pointer.Y - handle.End.Y) - RotateRingPixels)
                : DistanceToSegment(pointer, handle.Origin, handle.End);
            if (distance <= best)
            {
                best = distance;
                grab = new GizmoGrab((GizmoAxis)axis, pointer, handle,
                                     MathF.Atan2(pointer.Y - handle.Origin.Y, pointer.X - handle.Origin.X),
                                     AxisFacesCamera(camera, directions[axis]));
            }
        }
        return grab;
    }

    /// <summary>
    /// A grabbed drag's amount at <paramref name="pointer"/>: Translate, world units; Scale, a factor; Rotate, radians
    /// (negated when the axis faces the camera, as screen y points down).
    /// </summary>
    public static float DragAmount(GizmoMode mode, GizmoGrab grab, Vector2 pointer)
    {
        float movedX = pointer.X - grab.Start.X, movedY = pointer.Y - grab.Start.Y;
        float along = movedX * grab.Handle.Direction.X + movedY * grab.Handle.Direction.Y;
        switch (mode)
        {
            case GizmoMode.Translate: return along / grab.Handle.PixelsPerUnit;
            case GizmoMode.Scale: return 1.0f + along / HandlePixels;
            default:
            {
                float angle = MathF.Atan2(pointer.Y - grab.Handle.Origin.Y, pointer.X - grab.Handle.Origin.X) - grab.StartAngle;
                return grab.TowardCamera ? -angle : angle;
            }
        }
    }

    // std::round: halfway cases away from zero.
    private static float Round(float value) => MathF.Round(value, MidpointRounding.AwayFromZero);

    private static float PixelLength(float x, float y) => MathF.Sqrt(x * x + y * y);

    private static float DistanceToSegment(Vector2 p, Vector2 a, Vector2 b)
    {
        float abX = b.X - a.X, abY = b.Y - a.Y;
        float lengthSquared = abX * abX + abY * abY;
        float t = lengthSquared > 0.0f ? ((p.X - a.X) * abX + (p.Y - a.Y) * abY) / lengthSquared : 0.0f;
        t = Math.Clamp(t, 0.0f, 1.0f);
        return PixelLength(p.X - (a.X + abX * t), p.Y - (a.Y + abY * t));
    }

    private static float Dot(float[] a, float[] b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];

    private static float[] Cross(float[] a, float[] b) =>
        new[] { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };

    // The rotation by `angle` radians about world axis `axis`, column-major.
    private static float[] AxisRotation(GizmoAxis axis, float angle)
    {
        float c = MathF.Cos(angle), s = MathF.Sin(angle);
        return axis switch
        {
            GizmoAxis.X => new[] { 1.0f, 0.0f, 0.0f, 0.0f, c, s, 0.0f, -s, c },
            GizmoAxis.Y => new[] { c, 0.0f, -s, 0.0f, 1.0f, 0.0f, s, 0.0f, c },
            _ => new[] { c, s, 0.0f, -s, c, 0.0f, 0.0f, 0.0f, 1.0f },
        };
    }

    // a * b, both column-major 3x3, summed as the C++ model sums.
    private static float[] Multiply3(float[] a, float[] b)
    {
        var o = new float[9];
        for (int column = 0; column < 3; ++column)
        {
            for (int row = 0; row < 3; ++row)
            {
                float sum = 0.0f;
                for (int k = 0; k < 3; ++k) sum += a[k * 3 + row] * b[column * 3 + k];
                o[column * 3 + row] = sum;
            }
        }
        return o;
    }
}
