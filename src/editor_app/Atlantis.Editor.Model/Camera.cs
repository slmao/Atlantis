// Spec 0064 Phase 1 (Plan 0064 P4) and Plan 0068 M1 (Spec 0068 R1; ADR-0137
// D1): the E1 editor camera (src/editor/include/atlantis/editor/model/
// viewport_camera.h), ported to C# -- the host's camera convention (view,
// projection, NDC, view depth, pick ray) and every navigation of
// ViewportCameraModel (look, orbit, pan, dolly, dolly drag, fly, fly speed,
// frame). Client state only: the pose is handed to the Runtime with
// RemoteControl.SetViewportCamera (ADR-0128 D1). Held to the C++ model by
// tests/csharp/conformance/camera_vectors.jsonl. UI-free.
using System;
using System.Numerics;

namespace Atlantis.Editor.Model;

/// <summary>A camera's lens, as the World's Camera holds it.</summary>
public readonly record struct CameraLens(float FovYRadians, float NearZ, float FarZ)
{
    public static readonly CameraLens Default = new(1.0472f, 0.1f, 1000.0f);
}

/// <summary>
/// A camera as the host reads one: <c>WorldMatrix</c> is 16 floats, column-major; the eye is column 3, forward is
/// -column 2 (normalized); the other columns and any roll are ignored (the look-at uses world-up +Y).
/// </summary>
public sealed record CameraPose(float[] WorldMatrix, CameraLens Lens);

/// <summary>A camera's view and projection, column-major: look-at with world-up +Y, Vulkan clip space.</summary>
public sealed record CameraMatrices(float[] View, float[] Projection);

/// <summary>A world-space ray with a unit direction.</summary>
public readonly record struct PickRay(Vector3 Origin, Vector3 Direction);

public static class CameraConvention
{
    // The host's degeneracy threshold (scene_extraction.cpp, kDegenerateLengthEpsilon).
    private const float DegenerateLengthEpsilon = 1e-6f;

    /// <summary>The host's extraction, step for step; null exactly where the host's would fail.</summary>
    public static CameraMatrices? Matrices(CameraPose pose, float aspect)
    {
        float[] m = pose.WorldMatrix;
        var negColumn2 = new Vector3(-m[8], -m[9], -m[10]);
        float forwardLength = Length(negColumn2);
        if (forwardLength < DegenerateLengthEpsilon) return null;
        var forward = new Vector3(negColumn2.X / forwardLength, negColumn2.Y / forwardLength, negColumn2.Z / forwardLength);
        var eye = new Vector3(m[12], m[13], m[14]);
        if (Length(Cross(forward, Vector3.UnitY)) < DegenerateLengthEpsilon) return null;
        float[] view = LookAt(eye.X, eye.Y, eye.Z, eye.X + forward.X, eye.Y + forward.Y, eye.Z + forward.Z);
        float[] projection = Perspective(pose.Lens.FovYRadians, aspect, pose.Lens.NearZ, pose.Lens.FarZ);
        return new CameraMatrices(view, projection);
    }

    /// <summary>A world point to normalized device coordinates (x right, y down); null behind the camera.</summary>
    public static Vector2? ProjectToNdc(CameraMatrices camera, Vector3 point)
    {
        Span<float> view = stackalloc float[4];
        Span<float> clip = stackalloc float[4];
        float[] v = camera.View, p = camera.Projection;
        for (int row = 0; row < 4; ++row)
        {
            view[row] = v[0 * 4 + row] * point.X + v[1 * 4 + row] * point.Y + v[2 * 4 + row] * point.Z + v[3 * 4 + row];
        }
        for (int row = 0; row < 4; ++row)
        {
            clip[row] = p[0 * 4 + row] * view[0] + p[1 * 4 + row] * view[1] + p[2 * 4 + row] * view[2] + p[3 * 4 + row] * view[3];
        }
        if (!(clip[3] > 1e-6f)) return null;
        return new Vector2(clip[0] / clip[3], clip[1] / clip[3]);
    }

    /// <summary>A world point's distance in front of the camera (its -z in view space).</summary>
    public static float ViewDepth(CameraMatrices camera, Vector3 point)
    {
        float[] v = camera.View;
        return -(v[2] * point.X + v[6] * point.Y + v[10] * point.Z + v[14]);
    }

    /// <summary>
    /// The world-space ray through normalized device coordinates (x right, y down, both -1..1 across the view),
    /// from the eye.
    /// </summary>
    public static PickRay RayThroughNdc(CameraMatrices camera, float ndcX, float ndcY)
    {
        var v = new Vector3(ndcX / camera.Projection[0], ndcY / camera.Projection[5], -1.0f);
        float[] m = camera.View;
        Span<float> direction = stackalloc float[3];
        Span<float> origin = stackalloc float[3];
        for (int i = 0; i < 3; ++i)
        {
            direction[i] = m[i * 4 + 0] * v.X + m[i * 4 + 1] * v.Y + m[i * 4 + 2] * v.Z;
            origin[i] = -(m[i * 4 + 0] * m[12] + m[i * 4 + 1] * m[13] + m[i * 4 + 2] * m[14]);
        }
        var d = new Vector3(direction[0], direction[1], direction[2]);
        float length = Length(d);
        return new PickRay(new Vector3(origin[0], origin[1], origin[2]),
                           new Vector3(d.X * (1.0f / length), d.Y * (1.0f / length), d.Z * (1.0f / length)));
    }

    // The host's lookAtMatrix(), operation for operation.
    private static float[] LookAt(float eyeX, float eyeY, float eyeZ, float centerX, float centerY, float centerZ)
    {
        float fx = centerX - eyeX, fy = centerY - eyeY, fz = centerZ - eyeZ;
        float fLen = MathF.Sqrt(fx * fx + fy * fy + fz * fz);
        fx /= fLen;
        fy /= fLen;
        fz /= fLen;
        const float upX = 0.0f, upY = 1.0f, upZ = 0.0f;
        float sx = fy * upZ - fz * upY;
        float sy = fz * upX - fx * upZ;
        float sz = fx * upY - fy * upX;
        float sLen = MathF.Sqrt(sx * sx + sy * sy + sz * sz);
        sx /= sLen;
        sy /= sLen;
        sz /= sLen;
        float ux = sy * fz - sz * fy;
        float uy = sz * fx - sx * fz;
        float uz = sx * fy - sy * fx;
        var result = new float[] { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
        result[0] = sx;
        result[4] = sy;
        result[8] = sz;
        result[1] = ux;
        result[5] = uy;
        result[9] = uz;
        result[2] = -fx;
        result[6] = -fy;
        result[10] = -fz;
        result[12] = -(sx * eyeX + sy * eyeY + sz * eyeZ);
        result[13] = -(ux * eyeX + uy * eyeY + uz * eyeZ);
        result[14] = fx * eyeX + fy * eyeY + fz * eyeZ;
        return result;
    }

    // The host's perspectiveMatrix(): right-handed, depth [0, 1], y flipped.
    private static float[] Perspective(float fovYRadians, float aspect, float nearZ, float farZ)
    {
        float f = 1.0f / MathF.Tan(fovYRadians * 0.5f);
        var result = new float[16];
        result[0] = f / aspect;
        result[5] = -f;
        result[10] = farZ / (nearZ - farZ);
        result[11] = -1.0f;
        result[14] = (nearZ * farZ) / (nearZ - farZ);
        return result;
    }

    internal static float Length(Vector3 v) => MathF.Sqrt(v.X * v.X + v.Y * v.Y + v.Z * v.Z);

    internal static Vector3 Cross(Vector3 a, Vector3 b) =>
        new(a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X);
}

/// <summary>
/// The editor camera (E1's ViewportCameraModel): an eye, a yaw and a pitch (no roll), a pivot in front of the eye, a
/// fly speed and a lens. yaw = pitch = 0 looks down -z; positive yaw turns left, positive pitch looks up. Navigation
/// follows Spec 0062 R2: angles per pointer pixel, distances scaled by the pivot distance, motion in units per second.
/// Not thread-safe.
/// </summary>
public sealed class EditorCamera
{
    public const float MaxPitchRadians = 1.5533430f;     // 89 degrees
    public const float LookRadiansPerPixel = 0.0043633f;   // 0.25 degrees
    public const float OrbitRadiansPerPixel = 0.0052360f; // 0.3 degrees
    public const float MinPivotDistance = 0.05f;
    public const float DefaultPivotDistance = 10.0f;
    public const float WheelDollyFactor = 0.85f;          // per notch toward the pivot
    public const float DragDollyPerPixel = 0.01f;         // distance *= exp(-k * pixels)
    public const float MinFlySpeed = 0.05f;
    public const float MaxFlySpeed = 500.0f;
    public const float DefaultFlySpeed = 5.0f;            // units per second
    public const float FlySpeedStep = 1.25f;              // per wheel notch
    public const float FastFlyFactor = 4.0f;              // Shift

    private const float DegenerateLengthEpsilon = 1e-6f;

    private float[]? _resetWorldMatrix;  // cleared by every navigation
    private Vector3 _eye;
    private float _yaw;
    private float _pitch;
    private float _pivotDistance = DefaultPivotDistance;
    private float _flySpeed = DefaultFlySpeed;
    private CameraLens _lens = CameraLens.Default;

    public Vector3 Eye => _eye;
    public float Yaw => _yaw;
    public float Pitch => _pitch;
    public float PivotDistance => _pivotDistance;
    public float FlySpeed => _flySpeed;
    public CameraLens Lens => _lens;

    /// <summary>True once a navigation has moved the camera off the pose it was reset to.</summary>
    public bool Navigated => _resetWorldMatrix is null;

    /// <summary>Takes the pose's eye, direction (pitch clamped) and lens; the pivot distance and fly speed are kept.</summary>
    public void Reset(CameraPose pose)
    {
        float[] m = pose.WorldMatrix;
        _resetWorldMatrix = null;
        _eye = new Vector3(m[12], m[13], m[14]);
        _lens = pose.Lens;
        var negColumn2 = new Vector3(-m[8], -m[9], -m[10]);
        float length = CameraConvention.Length(negColumn2);
        if (!(length > DegenerateLengthEpsilon) || !float.IsFinite(length))
        {
            SetDirection(0.0f, 0.0f);
            return;
        }
        var f = new Vector3(negColumn2.X * (1.0f / length), negColumn2.Y * (1.0f / length), negColumn2.Z * (1.0f / length));
        float pitch = MathF.Asin(Math.Clamp(f.Y, -1.0f, 1.0f));
        SetDirection(MathF.Atan2(-f.X, -f.Z), pitch);
        if (pitch == _pitch) _resetWorldMatrix = (float[])m.Clone();
    }

    /// <summary>
    /// The pose the host renders: column 0 right, column 1 up, column 2 -forward, column 3 the eye. Until the first
    /// navigation after a reset whose pitch needed no clamp, the reset pose's own matrix, bit for bit.
    /// </summary>
    public CameraPose Pose()
    {
        if (_resetWorldMatrix is not null) return new CameraPose((float[])_resetWorldMatrix.Clone(), _lens);
        Vector3 f = Forward();
        Vector3 r = Right();
        Vector3 u = CameraConvention.Cross(r, f);
        return new CameraPose(new[] { r.X, r.Y, r.Z, 0.0f, u.X, u.Y, u.Z, 0.0f, -f.X, -f.Y, -f.Z, 0.0f, _eye.X, _eye.Y, _eye.Z, 1.0f },
                              _lens);
    }

    public CameraMatrices? Matrices(float aspect) => CameraConvention.Matrices(Pose(), aspect);

    public Vector3 Forward()
    {
        float c = MathF.Cos(_pitch);
        return new Vector3(-MathF.Sin(_yaw) * c, MathF.Sin(_pitch), -MathF.Cos(_yaw) * c);
    }

    /// <summary>Horizontal, unit.</summary>
    public Vector3 Right() => new(MathF.Cos(_yaw), 0.0f, -MathF.Sin(_yaw));

    /// <summary>The camera's up, unit.</summary>
    public Vector3 Up() => CameraConvention.Cross(Right(), Forward());

    public Vector3 Pivot() => Add(_eye, Scale(Forward(), _pivotDistance));

    /// <summary>Right-button drag: turns about the eye. Right looks right, down (positive dy) looks down.</summary>
    public void Look(float dxPixels, float dyPixels)
    {
        _resetWorldMatrix = null;
        SetDirection(_yaw - dxPixels * LookRadiansPerPixel, _pitch - dyPixels * LookRadiansPerPixel);
    }

    /// <summary>Alt + left drag: the same turn, about the pivot (the eye moves).</summary>
    public void Orbit(float dxPixels, float dyPixels)
    {
        _resetWorldMatrix = null;
        Vector3 center = Pivot();
        SetDirection(_yaw - dxPixels * OrbitRadiansPerPixel, _pitch - dyPixels * OrbitRadiansPerPixel);
        _eye = Add(center, Scale(Forward(), -_pivotDistance));
    }

    /// <summary>Middle drag: the point under the pointer at the pivot's depth follows the pointer.</summary>
    public void Pan(float dxPixels, float dyPixels, float viewportHeightPixels)
    {
        _resetWorldMatrix = null;
        if (!(viewportHeightPixels > 0.0f)) return;
        float unitsPerPixel = 2.0f * _pivotDistance * MathF.Tan(_lens.FovYRadians * 0.5f) / viewportHeightPixels;
        _eye = Add(_eye, Add(Scale(Right(), -dxPixels * unitsPerPixel), Scale(Up(), dyPixels * unitsPerPixel)));
    }

    /// <summary>The wheel: positive notches move toward the pivot.</summary>
    public void Dolly(float notches)
    {
        _resetWorldMatrix = null;
        Vector3 center = Pivot();
        _pivotDistance = MathF.Max(MinPivotDistance, _pivotDistance * MathF.Pow(WheelDollyFactor, notches));
        _eye = Add(center, Scale(Forward(), -_pivotDistance));
    }

    /// <summary>Alt + right drag: a drag down (positive dy) moves toward the pivot.</summary>
    public void DollyDrag(float dyPixels)
    {
        _resetWorldMatrix = null;
        Vector3 center = Pivot();
        _pivotDistance = MathF.Max(MinPivotDistance, _pivotDistance * MathF.Exp(-DragDollyPerPixel * dyPixels));
        _eye = Add(center, Scale(Forward(), -_pivotDistance));
    }

    /// <summary>
    /// Right button held + W/A/S/D/Q/E: <paramref name="direction"/> is (right, world-up, forward) in [-1, 1] each;
    /// moves fly speed (x FastFlyFactor when <paramref name="fast"/>) per second.
    /// </summary>
    public void Fly(Vector3 direction, float deltaSeconds, bool fast)
    {
        _resetWorldMatrix = null;
        if (!(deltaSeconds > 0.0f)) return;
        float distance = _flySpeed * (fast ? FastFlyFactor : 1.0f) * deltaSeconds;
        Vector3 move = Add(Add(Scale(Right(), direction.X), Scale(Vector3.UnitY, direction.Y)), Scale(Forward(), direction.Z));
        _eye = Add(_eye, Scale(move, distance));
    }

    /// <summary>Right button held + wheel.</summary>
    public void ScaleFlySpeed(float notches) =>
        _flySpeed = Math.Clamp(_flySpeed * MathF.Pow(FlySpeedStep, notches), MinFlySpeed, MaxFlySpeed);

    /// <summary>
    /// F: the pivot moves to <paramref name="center"/> and, when radius > 0, the distance becomes the one at which a
    /// sphere of that radius fits the vertical field of view.
    /// </summary>
    public void Frame(Vector3 center, float radius)
    {
        _resetWorldMatrix = null;
        if (radius > 0.0f && float.IsFinite(radius))
        {
            _pivotDistance = MathF.Max(MinPivotDistance, radius / MathF.Sin(_lens.FovYRadians * 0.5f));
        }
        _eye = Add(center, Scale(Forward(), -_pivotDistance));
    }

    private void SetDirection(float yaw, float pitch)
    {
        const float twoPi = 6.2831853f;
        _yaw = MathF.IEEERemainder(yaw, twoPi);
        _pitch = Math.Clamp(pitch, -MaxPitchRadians, MaxPitchRadians);
    }

    // Component-wise, as the C++ model's add()/scale() (no fused or reordered arithmetic).
    private static Vector3 Add(Vector3 a, Vector3 b) => new(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
    private static Vector3 Scale(Vector3 v, float s) => new(v.X * s, v.Y * s, v.Z * s);
}
