// Spec 0066 R1 / ADR-0132 D1 (Plan 0066 P2): a client's Viewport overlay -- the
// counterpart of the C++ connection::ViewportOverlay (viewport_overlay.h): a
// selection, outlined by the Runtime from the entities' meshes, and groups of
// primitives from a closed set, drawn by the Runtime after the scene. A group
// is world (its points are world positions) or screen-constant (an anchor: its
// points are offsets from the anchor, one unit drawn as PixelsPerUnit pixels at
// the anchor's view depth, from the frame's own camera). Set through
// RemoteControl.SetViewportOverlay. Plain immutable values.
using System;
using System.Collections.Generic;
using System.Numerics;

namespace Atlantis.Gameplay;

/// <summary>Straight alpha, authored in sRGB, each channel in [0, 1].</summary>
public readonly record struct OverlayColor(float R, float G, float B, float A = 1.0f);

/// <summary>Tested against the scene's depth, or drawn on top of it.</summary>
public enum OverlayDepth
{
    Tested,
    OnTop,
}

public sealed record OverlaySegment(Vector3 A, Vector3 B, OverlayColor Color, OverlayDepth Depth = OverlayDepth.Tested,
                                    float WidthPixels = 1.0f);

public sealed record OverlayTriangle(Vector3 A, Vector3 B, Vector3 C, OverlayColor Color, OverlayDepth Depth = OverlayDepth.Tested);

public sealed record OverlayPoint(Vector3 P, OverlayColor Color, OverlayDepth Depth = OverlayDepth.Tested, float SizePixels = 4.0f);

/// <summary>A screen-constant group's anchor (Spec 0066 Q2, S-a).</summary>
public readonly record struct OverlayAnchor(Vector3 Position, float PixelsPerUnit);

/// <summary>A world group (no anchor) or a screen-constant one.</summary>
public sealed record OverlayGroup(OverlayAnchor? Anchor, IReadOnlyList<OverlaySegment> Segments, IReadOnlyList<OverlayTriangle> Triangles,
                                  IReadOnlyList<OverlayPoint> Points)
{
    public static OverlayGroup World(IReadOnlyList<OverlaySegment>? segments = null, IReadOnlyList<OverlayTriangle>? triangles = null,
                                     IReadOnlyList<OverlayPoint>? points = null) =>
        new(null, segments ?? Array.Empty<OverlaySegment>(), triangles ?? Array.Empty<OverlayTriangle>(),
            points ?? Array.Empty<OverlayPoint>());
}

/// <summary>
/// One client's overlay. Limits per client (Plan 0066 J6): 256 groups, 65,536 segments, 65,536 triangles, 16,384
/// points and 256 selected entities; floats finite, colours in [0, 1], widths, sizes and scales positive -- else the
/// Runtime refuses it (InvalidRequest).
/// </summary>
public sealed record ViewportOverlay(IReadOnlyList<EntityGuid> Selection, IReadOnlyList<OverlayGroup> Groups)
{
    public static ViewportOverlay Select(params EntityGuid[] selection) => new(selection, Array.Empty<OverlayGroup>());
}
