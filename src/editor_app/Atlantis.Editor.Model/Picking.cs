// Plan 0068 M1 (Spec 0068 R1, R6; ADR-0137 D1): E1's Viewport picking math
// (src/editor/include/atlantis/editor/model/viewport_picking.h), ported to C#
// -- a world point's pixel, the ray through a pixel, and the markers of
// lights and cameras (entities without geometry), picked client-side. The
// geometry pick itself is the Runtime's (RemoteControl.Raycast, ADR-0128).
// Held to the C++ model by tests/csharp/conformance/picking_vectors.jsonl.
// UI-free.
using System;
using System.Collections.Generic;
using System.Numerics;
using Atlantis.Gameplay;

namespace Atlantis.Editor.Model;

/// <summary>The Viewport image's rectangle, in pixels.</summary>
public readonly record struct PixelRect(float X, float Y, float Width, float Height);

public enum MarkerKind
{
    Light,
    Camera,
}

/// <summary>An entity with a Light or a Camera and no Renderable, drawn at its world translation.</summary>
public readonly record struct Marker(EntityGuid Entity, MarkerKind Kind, Vector3 Position);

public static class Picking
{
    /// <summary>How close the pointer must be to a marker's pixel to pick it.</summary>
    public const float MarkerPickPixels = 10.0f;

    /// <summary>A world point's pixel inside <paramref name="rect"/>; null behind the camera.</summary>
    public static Vector2? PixelOf(CameraMatrices camera, PixelRect rect, Vector3 point)
    {
        Vector2? ndc = CameraConvention.ProjectToNdc(camera, point);
        if (ndc is not Vector2 n) return null;
        return new Vector2(rect.X + (n.X + 1.0f) * 0.5f * rect.Width, rect.Y + (n.Y + 1.0f) * 0.5f * rect.Height);
    }

    /// <summary>The world-space ray through pixel (px, py) of <paramref name="rect"/>.</summary>
    public static PickRay RayThroughPixel(CameraMatrices camera, PixelRect rect, float px, float py)
    {
        float ndcX = rect.Width > 0.0f ? (px - rect.X) / rect.Width * 2.0f - 1.0f : 0.0f;
        float ndcY = rect.Height > 0.0f ? (py - rect.Y) / rect.Height * 2.0f - 1.0f : 0.0f;
        return CameraConvention.RayThroughNdc(camera, ndcX, ndcY);
    }

    /// <summary>
    /// The marker within <see cref="MarkerPickPixels"/> of (px, py), the nearest to the camera (by view depth) when
    /// several are; a marker behind the camera is never hit.
    /// </summary>
    public static EntityGuid? PickMarker(IEnumerable<Marker> markers, CameraMatrices camera, PixelRect rect, float px, float py)
    {
        EntityGuid? best = null;
        float bestDepth = 0.0f;
        foreach (Marker marker in markers)
        {
            float depth = CameraConvention.ViewDepth(camera, marker.Position);
            if (!(depth > 0.0f)) continue;
            if (PixelOf(camera, rect, marker.Position) is not Vector2 pixel) continue;
            float dx = pixel.X - px;
            float dy = pixel.Y - py;
            if (dx * dx + dy * dy > MarkerPickPixels * MarkerPickPixels) continue;
            if (best is null || depth < bestDepth)
            {
                best = marker.Entity;
                bestDepth = depth;
            }
        }
        return best;
    }
}
