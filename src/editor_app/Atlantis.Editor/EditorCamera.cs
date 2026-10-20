// Spec 0064 Phase 1 (Plan 0064 P4; ADR-0125 D4): the E1 editor camera
// (src/editor/include/atlantis/editor/model/viewport_camera.h), ported to C#
// minimally -- the host's camera convention (view, projection, pick ray) and
// the orbit/dolly navigation. Client state only: the pose is handed to the
// Runtime with RemoteControl.SetViewportCamera (ADR-0128 D1). Held to the C++
// model by tests/csharp/conformance/camera_vectors.jsonl
// (src/editor_app/tests/Atlantis.Editor.Tests). UI-free.
using System;
using System.Numerics;

namespace Atlantis.Editor;

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
/// The editor camera (E1's ViewportCameraModel, orbit and dolly only): an eye, a yaw and a pitch (no roll), a pivot
/// in front of the eye and a lens. yaw = pitch = 0 looks down -z; positive yaw turns left, positive pitch looks up.
/// Not thread-safe.
/// </summary>
public sealed class OrbitCamera
{
    public const float MaxPitchRadians = 1.5533430f;     // 89 degrees
    public const float OrbitRadiansPerPixel = 0.0052360f; // 0.3 degrees
    public const float MinPivotDistance = 0.05f;
    public const float DefaultPivotDistance = 10.0f;
    public const float WheelDollyFactor = 0.85f;          // per notch toward the pivot

    private const float DegenerateLengthEpsilon = 1e-6f;

    private float[]? _resetWorldMatrix;  // cleared by every navigation
    private Vector3 _eye;
    private float _yaw;
    private float _pitch;
    private float _pivotDistance = DefaultPivotDistance;
    private CameraLens _lens = CameraLens.Default;

    public float Yaw => _yaw;
    public float Pitch => _pitch;
    public float PivotDistance => _pivotDistance;
    public CameraLens Lens => _lens;

    /// <summary>True once a navigation has moved the camera off the pose it was reset to.</summary>
    public bool Navigated => _resetWorldMatrix is null;

    /// <summary>Takes the pose's eye, direction (pitch clamped) and lens; the pivot distance is kept.</summary>
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

    public Vector3 Right() => new(MathF.Cos(_yaw), 0.0f, -MathF.Sin(_yaw));

    public Vector3 Pivot() => _eye + Scale(Forward(), _pivotDistance);

    /// <summary>A drag turns the camera about the pivot (the eye moves): right looks right, down looks down.</summary>
    public void Orbit(float dxPixels, float dyPixels)
    {
        _resetWorldMatrix = null;
        Vector3 center = Pivot();
        SetDirection(_yaw - dxPixels * OrbitRadiansPerPixel, _pitch - dyPixels * OrbitRadiansPerPixel);
        _eye = center + Scale(Forward(), -_pivotDistance);
    }

    /// <summary>The wheel: positive notches move toward the pivot.</summary>
    public void Dolly(float notches)
    {
        _resetWorldMatrix = null;
        Vector3 center = Pivot();
        _pivotDistance = MathF.Max(MinPivotDistance, _pivotDistance * MathF.Pow(WheelDollyFactor, notches));
        _eye = center + Scale(Forward(), -_pivotDistance);
    }

    private void SetDirection(float yaw, float pitch)
    {
        const float twoPi = 6.2831853f;
        _yaw = MathF.IEEERemainder(yaw, twoPi);
        _pitch = Math.Clamp(pitch, -MaxPitchRadians, MaxPitchRadians);
    }

    private static Vector3 Scale(Vector3 v, float s) => new(v.X * s, v.Y * s, v.Z * s);
}
