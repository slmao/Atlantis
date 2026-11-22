using System;
using System.Numerics;
using Atlantis.Gameplay;

namespace Atlantis.Editor.Model;

public sealed record PlacementRay(int X, int Y, int Width, int Height, long CameraIntent, CameraPose Pose, PickRay Ray)
{
    public ulong VisibilityIntent { get; init; }
}

/// <summary>Only the exact current hover sample may be displayed or admitted.</summary>
public sealed class MeshPlacementGesture(AssetDragIntent drag)
{
    private readonly object _gate = new();
    private PlacementRay? _hover, _released;
    public AssetDragIntent Drag { get; } = drag;

    public PlacementRay? Hover { get { lock (_gate) return _hover; } }

    public void Move(PlacementRay sample)
    {
        lock (_gate)
            if (_released is null) _hover = sample;
    }

    public void Leave() { lock (_gate) _hover = null; }

    public bool IsHover(PlacementRay sample)
    {
        lock (_gate) return ReferenceEquals(sample, _hover) && Drag.Matches(Drag.Token);
    }

    public bool Queue(PlacementRay sample)
    {
        lock (_gate)
        {
            if (!ReferenceEquals(sample, _hover) || !Drag.Queue()) return false;
            _released = sample;
            _hover = null;
            return true;
        }
    }

    public bool IsReleased(PlacementRay sample)
    {
        lock (_gate) return ReferenceEquals(sample, _released);
    }

    public void InvalidateRelease()
    {
        lock (_gate)
        {
            if (_released is null) return;
            // Cancel and the final transport send share the AssetDragIntent
            // gate, including a view change after the Worker's last check.
            Drag.Cancel();
            _released = null;
        }
    }
}

public sealed record MeshPlacementHit(MeshPlacementGesture Gesture, PlacementRay Sample, EntityGuid Surface,
    object SurfaceIdentity, float Distance, Vector3 Position, ulong DocumentRevision, ulong SceneGeneration);

public static class ViewportMeshPlacement
{
    public static Vector3? HitPosition(PlacementRay sample, RayHit? hit)
    {
        if (hit is null || !float.IsFinite(hit.Distance) || hit.Distance <= 0 ||
            sample.Width <= 0 || sample.Height <= 0 ||
            sample.X < 0 || sample.Y < 0 || sample.X >= sample.Width || sample.Y >= sample.Height ||
            NavigationFraming.Projection(sample.Pose, sample.Width, sample.Height) is not { } camera)
            return null;
        var position = sample.Ray.Origin + sample.Ray.Direction * hit.Distance;
        float depth = CameraConvention.ViewDepth(camera, position);
        return IsFinite(position) && float.IsFinite(depth) && depth >= sample.Pose.Lens.NearZ &&
            depth <= sample.Pose.Lens.FarZ ? position : null;
    }

    public static bool IsFinite(Vector3 point) =>
        float.IsFinite(point.X) && float.IsFinite(point.Y) && float.IsFinite(point.Z);

    public static OverlayGroup Marker(Vector3 position)
    {
        var color = new OverlayColor(0.42f, 0.78f, 1.0f);
        return new(new OverlayAnchor(position, 1.0f),
            [
                new(new(-10, 0, 0), new(10, 0, 0), color, OverlayDepth.OnTop, 2),
                new(new(0, -10, 0), new(0, 10, 0), color, OverlayDepth.OnTop, 2)
            ], [], [new(Vector3.Zero, color, OverlayDepth.OnTop, 5)]);
    }
}
