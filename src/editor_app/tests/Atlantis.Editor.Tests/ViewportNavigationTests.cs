using System;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Numerics;
using System.Threading;
using System.Threading.Channels;
using System.Threading.Tasks;
using Atlantis.Editor.Model;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Remote;

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
        CameraMotionDuringClear().GetAwaiter().GetResult();
    }

    private static async Task CameraMotionDuringClear()
    {
        var oldScene = new ViewportCamera(new float[] { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 15, 1 }, 1, 0.1f, 100);
        var newScene = oldScene with { WorldMatrix = new float[] { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 24, 1 } };
        var clearHeld = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var releaseClear = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var motionHeld = new TaskCompletionSource<ViewportCamera>(TaskCreationOptions.RunContinuationsAsynchronously);
        var releaseMotion = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var changes = Channel.CreateUnbounded<ViewportNavigationState>();
        using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(10));
        CancellationToken token = deadline.Token;
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var scene = AssetGuid.Parse("01090109-0010-4109-8109-000000000001");
        var info = new SessionInfo((ushort)((IPEndPoint)listener.LocalEndpoint).Port, "navigation-fixture", 1, scene);
        using var log = new EditorLog(Path.Combine(RuntimeProcess.NewWorkDirectory(), "navigation-pending.log"));
        using var worker = new EditorWorker(info, action => action(), log);
        worker.SetViewportExtent(800, 600);
        worker.NavigationChanged += state => changes.Writer.TryWrite(state);
        worker.Failed += message => changes.Writer.TryComplete(new InvalidOperationException(message));
        Task peer = Serve();
        worker.Start();
        try
        {
            await WaitState(0);
            var right = await WaitState(worker.Navigate(null, Array.Empty<HierarchyRow>(), 0, ViewDirection.Right));
            Check(right.EditorCamera is { } before && before.Forward().X < -0.99f, "establish previous Right camera");
            worker.Navigate(null, Array.Empty<HierarchyRow>(), 0, null);
            await clearHeld.Task.WaitAsync(token);

            var wheel = ViewportMotion.Dolly(1);
            var orbit = ViewportMotion.Orbit(6, -3);
            worker.QueueCameraMotion(wheel);
            long intent = worker.QueueCameraMotion(orbit);
            releaseClear.SetResult();

            var expected = new EditorCamera();
            expected.SeedNavigation(new CameraPose(newScene.WorldMatrix.ToArray(),
                new CameraLens(newScene.FovYRadians, newScene.NearZ, newScene.FarZ)));
            wheel.Apply(expected);
            orbit.Apply(expected);
            ViewportCamera sent = await motionHeld.Task.WaitAsync(token);
            Check(sent.WorldMatrix.SequenceEqual(expected.Pose().WorldMatrix),
                "wheel and orbit after delayed clear use the fresh scene camera in input order");
            Check(!changes.Reader.TryRead(out _), "motion projection stays unsettled while the next frame boundary is held");
            releaseMotion.SetResult();
            var settled = await WaitState(intent);
            Check(settled.ProjectionReady && settled.EditorCamera is { } camera &&
                  camera.Pose().WorldMatrix.SequenceEqual(expected.Pose().WorldMatrix),
                "only the settled replacement camera is published for picking");
        }
        finally
        {
            worker.Stop();
            deadline.Cancel();
            try { await peer; }
            catch (OperationCanceledException) when (token.IsCancellationRequested) { }
        }

        async Task<ViewportNavigationState> WaitState(long intent)
        {
            while (true)
            {
                var state = await changes.Reader.ReadAsync(token);
                if (state.Intent == intent) return state;
            }
        }

        async Task Serve()
        {
            using var connection = await listener.AcceptTcpClientAsync(token);
            using var stream = connection.GetStream();
            using var reader = new StreamReader(stream);
            using var writer = new StreamWriter(stream) { AutoFlush = true, NewLine = "\n" };
            bool cleared = false;
            ViewportCamera? motion = null;
            while (await reader.ReadLineAsync(token) is string line)
            {
                JsonValue request = Json.Parse(line);
                JsonValue result;
                switch (request.Find("method")!.AsString)
                {
                    case "hello": result = JsonValue.NewObject().Set("scene", Codec.Encode(scene)); break;
                    case "connection.schema":
                    case "connection.listEntities": result = JsonValue.NewArray(); break;
                    case "authoring.applyStatus": result = Codec.Err(JsonValue.String("UnknownApply")); break;
                    case "authoring.documentState":
                        var unsupported = JsonValue.NewObject().Set("id", request.Find("id")!).Set("error",
                            JsonValue.NewObject().Set("code", JsonValue.String("BadRequest"))
                                .Set("message", JsonValue.String("unknown method authoring.documentState")));
                        await writer.WriteLineAsync(Json.Write(unsupported).AsMemory(), token);
                        continue;
                    case "authoring.document": result = JsonValue.Null(); break;
                    case "control.viewportNavigation":
                        result = Codec.Ok(Codec.Encode(new ViewportNavigation(0, cleared ? newScene : oldScene, Array.Empty<ViewportBounds>())));
                        break;
                    case "control.setViewportCamera":
                        JsonValue pose = request.Find("params")!.Find("camera")!;
                        if (pose.IsNull)
                        {
                            cleared = true;
                            clearHeld.SetResult();
                            await releaseClear.Task.WaitAsync(token);
                        }
                        else if (cleared)
                            motion = Codec.DecodeViewportCamera(pose) ?? throw new InvalidOperationException("Invalid motion pose");
                        result = JsonValue.Null();
                        break;
                    case "control.status":
                        if (motion is not null && motionHeld.TrySetResult(motion))
                            await releaseMotion.Task.WaitAsync(token);
                        result = JsonValue.NewObject().Set("paused", JsonValue.Bool(false)).Set("frame", JsonValue.Number(1UL))
                            .Set("scene", Codec.Encode(scene)).Set("sceneGeneration", JsonValue.Number(0UL));
                        break;
                    default: throw new InvalidOperationException("Unexpected navigation request " + line);
                }
                var reply = JsonValue.NewObject().Set("id", request.Find("id")!).Set("result", result);
                await writer.WriteLineAsync(Json.Write(reply).AsMemory(), token);
            }
        }
    }
}
