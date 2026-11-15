using System;
using System.Numerics;

namespace Atlantis.Editor.Model;

public enum ViewDirection { Front, Back, Left, Right, Top, Bottom }

public readonly record struct ViewportMotion(float X, float Y, bool IsDolly)
{
    public static ViewportMotion Orbit(float x, float y) => new(x, y, false);
    public static ViewportMotion Dolly(float notches) => new(notches, 0, true);

    public void Apply(EditorCamera camera)
    {
        if (IsDolly) camera.Dolly(X);
        else camera.Orbit(X, Y);
    }
}

public static class NavigationFraming
{
    public static EditorCamera? Direction(EditorCamera current, ViewDirection direction, int width, int height)
    {
        const float halfTurn = MathF.PI;
        (float yaw, float pitch) = direction switch
        {
            ViewDirection.Back => (halfTurn, 0f),
            ViewDirection.Left => (-halfTurn / 2, 0f),
            ViewDirection.Right => (halfTurn / 2, 0f),
            ViewDirection.Top => (0f, -EditorCamera.MaxPitchRadians),
            ViewDirection.Bottom => (0f, EditorCamera.MaxPitchRadians),
            _ => (0f, 0f),
        };
        EditorCamera candidate = current.Copy();
        candidate.PlaceAtPivot(current.Pivot(), current.PivotDistance, yaw, pitch);
        return Projection(candidate.Pose(), width, height) is null ? null : candidate;
    }

    public static EditorCamera? Focus(EditorCamera current, Vector3 low, Vector3 high, int width, int height)
    {
        if (width <= 0 || height <= 0 || !Finite(low) || !Finite(high) ||
            low.X > high.X || low.Y > high.Y || low.Z > high.Z) return null;
        CameraLens lens = current.Lens;
        var pivot = new Vector3((float)(((double)low.X + high.X) / 2), (float)(((double)low.Y + high.Y) / 2),
                                (float)(((double)low.Z + high.Z) / 2));
        double x = Math.Max((double)pivot.X - low.X, (double)high.X - pivot.X);
        double y = Math.Max((double)pivot.Y - low.Y, (double)high.Y - pivot.Y);
        double z = Math.Max((double)pivot.Z - low.Z, (double)high.Z - pivot.Z);
        double radius = 1.1 * Math.Sqrt(x * x + y * y + z * z);
        double v = lens.FovYRadians / 2.0;
        double h = Math.Atan(Math.Tan(v) * width / height);
        double near = lens.NearZ + radius;
        double far = lens.FarZ - radius;
        if (!(near < far)) return null;
        double distance = radius > 0
            ? Math.Max(Math.Max(near, radius / Math.Sin(Math.Min(v, h))), EditorCamera.MinPivotDistance)
            : Math.Clamp(current.PivotDistance, near + (far - near) * 0.001, far - (far - near) * 0.001);
        if (!double.IsFinite(distance) || distance > far) return null;
        EditorCamera candidate = current.Copy();
        candidate.PlaceAtPivot(pivot, distance);
        if (Projection(candidate.Pose(), width, height) is not CameraMatrices projection) return null;
        for (int corner = 0; corner < 8; ++corner)
        {
            var point = new Vector3((corner & 1) == 0 ? low.X : high.X, (corner & 2) == 0 ? low.Y : high.Y,
                                    (corner & 4) == 0 ? low.Z : high.Z);
            if (CameraConvention.ProjectToNdc(projection, point) is not Vector2 ndc ||
                !float.IsFinite(ndc.X) || !float.IsFinite(ndc.Y) || MathF.Abs(ndc.X) > 1 || MathF.Abs(ndc.Y) > 1)
                return null;
            float depth = CameraConvention.ViewDepth(projection, point);
            if (!float.IsFinite(depth) || depth <= lens.NearZ || depth >= lens.FarZ) return null;
        }
        return candidate;
    }

    public static CameraMatrices? Projection(CameraPose pose, int width, int height)
    {
        CameraLens lens = pose.Lens;
        if (width <= 0 || height <= 0 || !float.IsFinite(lens.FovYRadians) || lens.FovYRadians <= 0 ||
            lens.FovYRadians >= 3.1415926f || !float.IsFinite(lens.NearZ) || !float.IsFinite(lens.FarZ) ||
            lens.NearZ <= 0 || lens.FarZ <= lens.NearZ) return null;
        foreach (float value in pose.WorldMatrix)
            if (!float.IsFinite(value)) return null;
        CameraMatrices? matrices = CameraConvention.Matrices(pose, (float)width / height);
        if (matrices is null) return null;
        foreach (float value in matrices.View)
            if (!float.IsFinite(value)) return null;
        foreach (float value in matrices.Projection)
            if (!float.IsFinite(value)) return null;
        return matrices;
    }

    private static bool Finite(Vector3 value) =>
        float.IsFinite(value.X) && float.IsFinite(value.Y) && float.IsFinite(value.Z);
}
