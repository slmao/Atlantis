// Spec 0064 Phase 1 (Plan 0064 M5): the headless smoke -- the shell's own
// code paths without embedding: start atlantis_runtime --listen (a top-level
// window), read the session file, connect, list the entities, set an orbited
// camera and check the frame's view is the C# convention's, ray-cast a hit
// and a miss, clear the camera (the scene camera's view again), and close the
// Runtime cleanly. Prints one JSON object; exit 0 when every check passed.
// Plan 0066 M5 (P5): before closing, the hit's overlay (GizmoOverlay.ForPick)
// is sent, and a capture with withOverlay differs from one without at the
// picked entity's pixel; once cleared, a capture asking for it shows none.
using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Numerics;
using System.Text;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Remote;
using W = Atlantis.Gameplay.World;

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

                EntityGuid? picked = null;
                if (snapshot.FirstRenderablePosition is Vector3 target)
                {
                    var eye = new Vector3(scenePose.WorldMatrix[12], scenePose.WorldMatrix[13], scenePose.WorldMatrix[14]);
                    Vector3 direction = Vector3.Normalize(target - eye);
                    RaycastResult hit = session.Control.Raycast(new Ray(eye, direction));
                    Check("a ray at a Renderable hits an entity",
                          hit.IsOk && hit.Hit is RayHit h && snapshot.Entities.Any(row => row.Guid == h.Entity),
                          hit.Error?.ToString() ?? hit.Hit?.ToString() ?? "miss");
                    picked = hit.Hit?.Entity;
                }
                RaycastResult miss = session.Control.Raycast(new Ray(new Vector3(0.0f, 1.0e4f, 0.0f), Vector3.UnitY));
                Check("a ray into the sky misses", miss.IsOk && miss.Hit is null, miss.Error?.ToString() ?? miss.Hit?.ToString() ?? "miss");

                session.Control.SetViewportCamera(null);
                FrameData restored = session.Control.Step(new StepRequest(1)).Report!.Data;
                Check("clearing the camera restores the scene camera's view", restored.View.SequenceEqual(sceneView));

                // Plan 0066 M5 (P5): the pick's overlay, seen only by a capture that asks for it (withOverlay).
                Check("a pick to outline", picked is not null);
                if (picked is EntityGuid entity)
                {
                    Vector4 column3 = world.Get<W.WorldMatrix>(entity).Column3;
                    var anchor = new Vector3(column3.X, column3.Y, column3.Z);
                    ControlError? set = session.Control.SetViewportOverlay(GizmoOverlay.ForPick(entity, anchor));
                    Check("the pick's overlay is accepted", set is null, set?.ToString() ?? "");
                    StepResult plain = session.Control.Step(new StepRequest(1, Path.Combine(work, "smoke_plain.png")));
                    StepResult shown = session.Control.Step(new StepRequest(1, Path.Combine(work, "smoke_overlay.png"), WithOverlay: true));
                    Check("captures with and without the overlay", plain.Report?.Image is not null && shown.Report?.Image is not null,
                          plain.Error?.ToString() ?? shown.Error?.ToString() ?? "");
                    if (plain.Report?.Image is CapturedImage plainImage && shown.Report?.Image is CapturedImage shownImage)
                    {
                        PngPixels without = PngPixels.Read(plainImage.Path);
                        PngPixels with = PngPixels.Read(shownImage.Path);
                        (int X, int Y)? at = Project(shown.Report.Data, anchor, with.Width, with.Height);
                        Check("the picked entity is in view", at is not null && with.Width == without.Width && with.Height == without.Height,
                              at?.ToString() ?? "off screen");
                        if (at is { } pixel)
                        {
                            Check("the picked entity's pixels differ with the overlay (its Gizmo)",
                                  with.Rgba(pixel.X, pixel.Y) != without.Rgba(pixel.X, pixel.Y),
                                  string.Format(CultureInfo.InvariantCulture, "at {0}: {1:x8} without, {2:x8} with", pixel,
                                                without.Rgba(pixel.X, pixel.Y), with.Rgba(pixel.X, pixel.Y)));
                        }
                    }
                    ControlError? cleared = session.Control.SetViewportOverlay(null);
                    StepResult after = session.Control.Step(new StepRequest(1, Path.Combine(work, "smoke_cleared.png"), WithOverlay: true));
                    Check("a cleared overlay: a capture asking for it shows none",
                          cleared is null && after.Report?.Image is CapturedImage afterImage && plain.Report?.Image is CapturedImage again &&
                          PngPixels.Read(afterImage.Path).SamePixels(PngPixels.Read(again.Path)),
                          cleared?.ToString() ?? after.Error?.ToString() ?? "");
                }
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

    // The pixel a world point is drawn at, from the frame's own matrices (column-major; the Vulkan convention the
    // pick ray inverts: NDC y grows downward, as pixel rows do). Null behind the camera or outside the image.
    private static (int X, int Y)? Project(FrameData frame, Vector3 p, int width, int height)
    {
        IReadOnlyList<float> v = frame.View, m = frame.Projection;
        var view = new Vector4(v[0] * p.X + v[4] * p.Y + v[8] * p.Z + v[12], v[1] * p.X + v[5] * p.Y + v[9] * p.Z + v[13],
                               v[2] * p.X + v[6] * p.Y + v[10] * p.Z + v[14], 1.0f);
        float x = m[0] * view.X + m[4] * view.Y + m[8] * view.Z + m[12];
        float y = m[1] * view.X + m[5] * view.Y + m[9] * view.Z + m[13];
        float w = m[3] * view.X + m[7] * view.Y + m[11] * view.Z + m[15];
        if (w <= 0.0f) return null;
        int px = (int)MathF.Floor((x / w + 1.0f) * 0.5f * width);
        int py = (int)MathF.Floor((y / w + 1.0f) * 0.5f * height);
        return px >= 0 && py >= 0 && px < width && py < height ? (px, py) : null;
    }

    private static string Describe(IReadOnlyList<float> actual, float[] expected) =>
        "got [" + string.Join(",", actual.Select(x => x.ToString("G6", CultureInfo.InvariantCulture))) + "] expected [" +
        string.Join(",", expected.Select(x => x.ToString("G6", CultureInfo.InvariantCulture))) + "]";
}

/// <summary>
/// Plan 0066 M5: a capture's pixels -- the Runtime's PNG (frame_capture.cpp, stb_image_write: 8-bit RGBA,
/// not interlaced), decoded with the base library's zlib; nothing else is read.
/// </summary>
public sealed class PngPixels
{
    private readonly byte[] _rgba;

    private PngPixels(int width, int height, byte[] rgba)
    {
        Width = width;
        Height = height;
        _rgba = rgba;
    }

    public int Width { get; }
    public int Height { get; }

    /// <summary>The pixel as 0xRRGGBBAA.</summary>
    public uint Rgba(int x, int y) => BinaryPrimitives.ReadUInt32BigEndian(_rgba.AsSpan((y * Width + x) * 4, 4));

    public bool SamePixels(PngPixels other) => Width == other.Width && Height == other.Height && _rgba.AsSpan().SequenceEqual(other._rgba);

    public static PngPixels Read(string path)
    {
        byte[] file = File.ReadAllBytes(path);
        ReadOnlySpan<byte> signature = new byte[] { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
        if (file.Length < 8 || !file.AsSpan(0, 8).SequenceEqual(signature)) throw new InvalidDataException(path + ": not a PNG");
        int width = 0, height = 0;
        using var idat = new MemoryStream();
        for (int at = 8; at + 12 <= file.Length;)
        {
            int length = BinaryPrimitives.ReadInt32BigEndian(file.AsSpan(at, 4));
            string type = Encoding.ASCII.GetString(file, at + 4, 4);
            ReadOnlySpan<byte> data = file.AsSpan(at + 8, length);
            if (type == "IHDR")
            {
                width = BinaryPrimitives.ReadInt32BigEndian(data[..4]);
                height = BinaryPrimitives.ReadInt32BigEndian(data.Slice(4, 4));
                if (data[8] != 8 || data[9] != 6 || data[12] != 0) throw new InvalidDataException(path + ": not 8-bit RGBA, non-interlaced");
            }
            else if (type == "IDAT")
            {
                idat.Write(data);
            }
            else if (type == "IEND")
            {
                break;
            }
            at += 12 + length;
        }
        int stride = width * 4;
        var filtered = new byte[(stride + 1) * height];
        idat.Position = 0;
        using (var zlib = new ZLibStream(idat, CompressionMode.Decompress)) zlib.ReadExactly(filtered);
        var rgba = new byte[stride * height];
        for (int y = 0; y < height; ++y)
        {
            byte filter = filtered[y * (stride + 1)];
            ReadOnlySpan<byte> row = filtered.AsSpan(y * (stride + 1) + 1, stride);
            Span<byte> output = rgba.AsSpan(y * stride, stride);
            ReadOnlySpan<byte> above = y > 0 ? rgba.AsSpan((y - 1) * stride, stride) : new byte[stride];
            for (int i = 0; i < stride; ++i)
            {
                int left = i >= 4 ? output[i - 4] : 0;
                int up = above[i];
                int upLeft = i >= 4 ? above[i - 4] : 0;
                int predicted = filter switch
                {
                    0 => 0,
                    1 => left,
                    2 => up,
                    3 => (left + up) / 2,
                    4 => Paeth(left, up, upLeft),
                    _ => throw new InvalidDataException(path + ": filter " + filter),
                };
                output[i] = (byte)(row[i] + predicted);
            }
        }
        return new PngPixels(width, height, rgba);
    }

    private static int Paeth(int a, int b, int c)
    {
        int p = a + b - c;
        int pa = Math.Abs(p - a), pb = Math.Abs(p - b), pc = Math.Abs(p - c);
        return pa <= pb && pa <= pc ? a : (pb <= pc ? b : c);
    }
}
