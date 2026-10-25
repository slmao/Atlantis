// Plan 0066 M5 (Spec 0066 R7, P5): the overlay the editor sends for a pick --
// the picked entity's outline (its selection) and a translate Gizmo at its
// WorldMatrix's translation, drawn by the Runtime after the scene. The Gizmo
// is one screen-constant group (Spec 0066 Q2, S-a) anchored at the
// translation: three axis segments along the world's axes, each with a cone
// tip of triangles, all on top. One unit of the group is HandlePixels pixels
// at the anchor's depth, so an axis parallel to the image plane is as long
// on screen as E1's ImGui handle (src/editor/src/view/editor_view.cpp:
// kHandlePixels, the 3-pixel line, the 10-pixel tip 5 pixels wide each side,
// kAxisColors). Draws only: no hit-testing, no editing (Spec 0066 Q4, Q5).
// UI-free.
using System;
using System.Collections.Generic;
using System.Numerics;
using Atlantis.Gameplay;

namespace Atlantis.Editor;

public static class GizmoOverlay
{
    // E1's convention (editor_view.cpp), in pixels.
    public const float HandlePixels = 80.0f;
    public const float LineWidthPixels = 3.0f;
    public const float TipLengthPixels = 10.0f;
    public const float TipRadiusPixels = 5.0f;
    // The cone's sides; each side is a mantle triangle and a base triangle.
    public const int TipSides = 8;

    // kAxisColors: IM_COL32(230, 60, 60), (70, 200, 70), (70, 120, 240), opaque.
    public static readonly OverlayColor[] AxisColors =
    {
        new(230 / 255.0f, 60 / 255.0f, 60 / 255.0f),
        new(70 / 255.0f, 200 / 255.0f, 70 / 255.0f),
        new(70 / 255.0f, 120 / 255.0f, 240 / 255.0f),
    };

    private static readonly Vector3[] Axes = { Vector3.UnitX, Vector3.UnitY, Vector3.UnitZ };

    /// <summary>The picked entity outlined, with a translate Gizmo at <paramref name="position"/>.</summary>
    public static ViewportOverlay ForPick(EntityGuid entity, Vector3 position) =>
        new(new[] { entity }, new[] { TranslateGizmo(position) });

    /// <summary>
    /// The translate Gizmo anchored at <paramref name="position"/>: per axis (X, Y, Z) one segment from the anchor to
    /// one unit along it, then its cone's 2 * TipSides triangles; one unit is HandlePixels pixels.
    /// </summary>
    public static OverlayGroup TranslateGizmo(Vector3 position)
    {
        const float tipLength = TipLengthPixels / HandlePixels;
        const float tipRadius = TipRadiusPixels / HandlePixels;
        var segments = new List<OverlaySegment>(3);
        var triangles = new List<OverlayTriangle>(3 * 2 * TipSides);
        for (int axis = 0; axis < 3; ++axis)
        {
            Vector3 along = Axes[axis];
            Vector3 u = Axes[(axis + 1) % 3];
            Vector3 v = Axes[(axis + 2) % 3];
            OverlayColor color = AxisColors[axis];
            segments.Add(new OverlaySegment(Vector3.Zero, along, color, OverlayDepth.OnTop, LineWidthPixels));
            Vector3 tip = along * (1.0f + tipLength);
            for (int side = 0; side < TipSides; ++side)
            {
                Vector3 rim0 = Rim(along, u, v, tipRadius, side);
                Vector3 rim1 = Rim(along, u, v, tipRadius, side + 1);
                triangles.Add(new OverlayTriangle(tip, rim0, rim1, color, OverlayDepth.OnTop));
                triangles.Add(new OverlayTriangle(along, rim1, rim0, color, OverlayDepth.OnTop));
            }
        }
        return new OverlayGroup(new OverlayAnchor(position, HandlePixels), segments, triangles, Array.Empty<OverlayPoint>());
    }

    private static Vector3 Rim(Vector3 along, Vector3 u, Vector3 v, float radius, int side)
    {
        float angle = 2.0f * MathF.PI * (side % TipSides) / TipSides;
        return along + (u * MathF.Cos(angle) + v * MathF.Sin(angle)) * radius;
    }
}
