using System;
using System.Numerics;
using Atlantis.Editor.Model;

namespace Atlantis.Editor.Tests;

public static partial class Program
{
    private static void ViewportNavigationChecks()
    {
        EditorCamera camera = new();
        var boundsLow = new Vector3(-3, -1, -0.5f);
        var boundsHigh = new Vector3(5, 1, 0.5f);
        var pivot = (boundsLow + boundsHigh) / 2;
        foreach ((int width, int height) in new[] { (1220, 680), (320, 640) })
        {
            EditorCamera? focus = NavigationFraming.Focus(camera, boundsLow, boundsHigh, width, height);
            Check(focus is not null && Vector3.Distance(focus.Pivot(), pivot) < 0.001f, "focus centers the mixed selection");
            Check(focus is not null && NavigationFraming.Projection(focus.Pose(), width, height) is CameraMatrices m &&
                CameraConvention.ProjectToNdc(m, boundsLow) is Vector2 low && MathF.Abs(low.X) < 1 &&
                CameraConvention.ProjectToNdc(m, boundsHigh) is Vector2 high && MathF.Abs(high.X) < 1,
                "both corners fit the actual aspect");
        }
        Vector3[] expected = [-Vector3.UnitZ, Vector3.UnitZ, Vector3.UnitX, -Vector3.UnitX,
            new(0, -MathF.Sin(EditorCamera.MaxPitchRadians), -MathF.Cos(EditorCamera.MaxPitchRadians)),
            new(0, MathF.Sin(EditorCamera.MaxPitchRadians), -MathF.Cos(EditorCamera.MaxPitchRadians))];
        foreach (ViewDirection direction in Enum.GetValues<ViewDirection>())
        {
            EditorCamera? view = NavigationFraming.Direction(camera, direction, 800, 600);
            Check(view is not null && Vector3.Distance(view.Forward(), expected[(int)direction]) < 0.001f &&
                Vector3.Distance(view.Pivot(), camera.Pivot()) < 0.001f, "direction preserves pivot and canonical forward " + direction);
            view?.Orbit(4, 1);
            Check(view is not null && NavigationFraming.Projection(view.Pose(), 800, 600) is not null, "orbit after " + direction);
        }
        var point = new Vector3(15, 2, 1);
        Check(NavigationFraming.Focus(camera, point, point, 700, 400) is not null, "off-origin coincident selection");
        Check(NavigationFraming.Focus(camera, boundsLow, boundsHigh, 0, 600) is null, "collapsed viewport refused");
        Check(NavigationFraming.Focus(camera, new Vector3(-1e20f), new Vector3(1e20f), 800, 600) is null, "finite extreme clip failure");
        var extreme = new Vector3(1e20f);
        Check(NavigationFraming.Focus(camera, extreme, extreme, 800, 600) is null, "finite eye whose eye-plus-forward collapses");
    }
}
