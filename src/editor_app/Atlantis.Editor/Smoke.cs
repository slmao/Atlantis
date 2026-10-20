// Spec 0064 Phase 1 (Plan 0064 M5): the headless smoke -- the shell's own
// code paths without embedding: start atlantis_runtime --listen (a top-level
// window), read the session file, connect, list the entities, set an orbited
// camera and check the frame's view is the C# convention's, ray-cast a hit
// and a miss, clear the camera (the scene camera's view again), and close the
// Runtime cleanly. Prints one JSON object; exit 0 when every check passed.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Text;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Remote;

namespace Atlantis.Editor;

public static class Smoke
{
    public static int Run(string runtimePath, string scene, string? outPath)
    {
        var checks = new List<(string Name, bool Ok, string Detail)>();
        void Check(string name, bool ok, string detail = "") => checks.Add((name, ok, detail));
        string work = RuntimeProcess.NewWorkDirectory();
        int? exitCode = null;
        bool sessionRemoved = false;
        try
        {
            using RuntimeProcess runtime = RuntimeProcess.Start(new RuntimeLaunch(runtimePath, scene, null, work));
            SessionInfo info = runtime.WaitForSession(TimeSpan.FromSeconds(120));
            Check("session file", info.Window is null, "port " + info.Port);
            using (RemoteSession session = RemoteSession.Connect(info))
            {
                var world = new GameplayWorld(session);
                SceneSnapshot snapshot = SceneReader.Read(world);
                Check("entities listed", snapshot.Entities.Count > 0, snapshot.Entities.Count + " entities");
                Check("scene camera read", snapshot.ScenePose is not null);
                CameraPose scenePose = snapshot.ScenePose ?? throw new InvalidOperationException("no scene camera");

                StepResult before = session.Control.Step(new StepRequest(1));
                Check("step", before.IsOk, before.Error?.ToString() ?? "");
                IReadOnlyList<float> sceneView = before.Report!.Data.View;
                CameraMatrices expectedScene = CameraConvention.Matrices(scenePose, 1.0f)!;
                Check("the scene camera's view is the C# convention's", Near(sceneView, expectedScene.View),
                      Describe(sceneView, expectedScene.View));

                var camera = new OrbitCamera();
                camera.Reset(scenePose);
                camera.Orbit(30.0f, -10.0f);
                camera.Dolly(1.0f);
                session.Control.SetViewportCamera(SceneReader.ToViewportCamera(camera.Pose()));
                FrameData drawn = session.Control.Step(new StepRequest(1)).Report!.Data;
                CameraMatrices expected = camera.Matrices(1.0f)!;
                Check("an orbited camera draws the frame (view)", Near(drawn.View, expected.View), Describe(drawn.View, expected.View));
                float f = 1.0f / MathF.Tan(camera.Lens.FovYRadians * 0.5f);
                Check("an orbited camera draws the frame (lens)", MathF.Abs(drawn.Projection[5] + f) <= 1e-4f * f,
                      drawn.Projection[5].ToString(CultureInfo.InvariantCulture));

                if (snapshot.FirstRenderablePosition is Vector3 target)
                {
                    var eye = new Vector3(scenePose.WorldMatrix[12], scenePose.WorldMatrix[13], scenePose.WorldMatrix[14]);
                    Vector3 direction = Vector3.Normalize(target - eye);
                    RaycastResult hit = session.Control.Raycast(new Ray(eye, direction));
                    Check("a ray at a Renderable hits an entity",
                          hit.IsOk && hit.Hit is RayHit h && snapshot.Entities.Any(row => row.Guid == h.Entity),
                          hit.Error?.ToString() ?? hit.Hit?.ToString() ?? "miss");
                }
                RaycastResult miss = session.Control.Raycast(new Ray(new Vector3(0.0f, 1.0e4f, 0.0f), Vector3.UnitY));
                Check("a ray into the sky misses", miss.IsOk && miss.Hit is null, miss.Error?.ToString() ?? miss.Hit?.ToString() ?? "miss");

                session.Control.SetViewportCamera(null);
                FrameData restored = session.Control.Step(new StepRequest(1)).Report!.Data;
                Check("clearing the camera restores the scene camera's view", restored.View.SequenceEqual(sceneView));
            }
            Check("the Runtime closes", runtime.CloseMainWindow());
            exitCode = runtime.WaitForExit(TimeSpan.FromSeconds(30));
            Check("the Runtime exits 0", exitCode == 0, exitCode?.ToString(CultureInfo.InvariantCulture) ?? "killed");
            sessionRemoved = !File.Exists(runtime.SessionPath);
            Check("the session file is removed", sessionRemoved);
        }
        catch (Exception e)
        {
            Check("no exception", false, e.GetType().Name + ": " + e.Message);
        }

        var json = new StringBuilder("{\"smoke\":\"atlantis_editor\",\"scene\":\"" + scene + "\",\"logs\":\"" +
                                     work.Replace("\\", "\\\\") + "\",\"checks\":[");
        for (int i = 0; i < checks.Count; ++i)
        {
            if (i > 0) json.Append(',');
            json.Append("{\"name\":\"").Append(checks[i].Name).Append("\",\"ok\":").Append(checks[i].Ok ? "true" : "false")
                .Append(",\"detail\":\"").Append(checks[i].Detail.Replace("\\", "\\\\").Replace("\"", "'")).Append("\"}");
        }
        bool passed = checks.Count > 0 && checks.All(c => c.Ok);
        json.Append("],\"passed\":").Append(passed ? "true" : "false").Append('}');
        Program.Write(outPath, json.ToString());
        foreach ((string name, bool ok, string detail) in checks)
        {
            Console.Error.WriteLine((ok ? "  ok    " : "  FAIL  ") + name + (detail.Length > 0 ? " -- " + detail : ""));
        }
        return passed ? 0 : 1;
    }

    private static bool Near(IReadOnlyList<float> actual, float[] expected)
    {
        if (actual.Count != expected.Length) return false;
        for (int i = 0; i < expected.Length; ++i)
        {
            if (MathF.Abs(actual[i] - expected[i]) > 1e-4f * MathF.Max(1.0f, MathF.Abs(expected[i]))) return false;
        }
        return true;
    }

    private static string Describe(IReadOnlyList<float> actual, float[] expected) =>
        "got [" + string.Join(",", actual.Select(x => x.ToString("G6", CultureInfo.InvariantCulture))) + "] expected [" +
        string.Join(",", expected.Select(x => x.ToString("G6", CultureInfo.InvariantCulture))) + "]";
}
