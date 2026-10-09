// GameplayDemo (Spec 0058 R5, ruling Q6; ADR-0112 D5; Plan 0058 P8): the C#
// Gameplay SDK's beacon demo. It attaches to a running
// `atlantis_runtime --listen` over atlantis.remote/1, holds nothing of the
// engine, and drives a "beacon" point light through the operation loop with
// the typed layer -- find, spawn as one transaction, move and pulse it once
// per logic step, a refused transaction, a capture, destroy -- checking every
// stepped frame's data exactly. One line per step; exit 0 only if every check
// holds.
//
// The logic's time is its own step k: the Runtime is paused and stepped one
// frame per k. The beacon walks the square (1.5,2,1.5) (-1.5,2,1.5)
// (-1.5,2,-1.5) (1.5,2,-1.5) and pulses between 4 and 6, computed with
// + - * /, Abs and casts only, so every value is the same on every run; with
// the default K = 8 every position and intensity is exact (corners, edge
// midpoints, 4 or 6). The output names no frame number, path or time, so it
// is identical run to run -- provided no other client resumes or steps the
// Runtime meanwhile (Spec 0057 R10).
//
//   dotnet GameplayDemo.dll [--session <path>] [--steps <K>] [--capture-dir <dir>]
//
// Exit codes: 0 every check passed; 1 a check failed (or a beacon from an
// aborted run is still there); 2 usage; 3 session, connection or a schema
// mismatch.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Numerics;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Remote;
using W = Atlantis.Gameplay.World;

namespace Atlantis.Gameplay.Examples;

public static class Program
{
    public static int Main(string[] args)
    {
        string? session = null;
        int steps = 8;
        string captureDir = Path.Combine(Path.GetTempPath(), "atlantis_csharp_gameplay_demo");
        for (int i = 0; i < args.Length; ++i)
        {
            if (i + 1 >= args.Length) return Usage();
            switch (args[i])
            {
                case "--session": session = args[++i]; break;
                case "--steps":
                    if (!int.TryParse(args[++i], NumberStyles.None, CultureInfo.InvariantCulture, out steps) || steps < 1) return Usage();
                    break;
                case "--capture-dir": captureDir = args[++i]; break;
                default: return Usage();
            }
        }

        string path = SessionFile.Resolve(session);
        RemoteSession remote;
        try
        {
            remote = RemoteSession.Connect(SessionFile.Read(path));
        }
        catch (SessionFileException)
        {
            Console.WriteLine("1 connect: FAILED no attachable Runtime (" + path + ")");
            return 3;
        }
        catch (RemoteException e)
        {
            Console.WriteLine("1 connect: FAILED " + e.Error);
            return 3;
        }
        using (remote)
        {
            Directory.CreateDirectory(captureDir);
            return new Demo(remote, steps, captureDir).Run();
        }
    }

    private static int Usage()
    {
        Console.Error.WriteLine("usage: dotnet GameplayDemo.dll [--session <path>] [--steps <K>] [--capture-dir <dir>]");
        Console.Error.WriteLine("  Attaches to a running `atlantis_runtime --listen` and drives a beacon light through the");
        Console.Error.WriteLine("  C# Gameplay SDK's operation loop. Assumes no other client resumes or steps the Runtime meanwhile.");
        return 2;
    }
}

internal sealed class StepFailed : Exception
{
    public StepFailed(string step, string why, int code) : base(why)
    {
        Step = step;
        Code = code;
    }

    public string Step { get; }
    public int Code { get; }
}

internal sealed class Demo
{
    // Plan 0058 P8: fixed, so the output is identical run to run.
    private static readonly EntityGuid Beacon = EntityGuid.Parse("58005800-0000-4000-8000-0000000000b1");

    private static readonly Vector3[] Corners =
    {
        new(1.5f, 2.0f, 1.5f), new(-1.5f, 2.0f, 1.5f), new(-1.5f, 2.0f, -1.5f), new(1.5f, 2.0f, -1.5f),
    };

    private static readonly Vector3 BeaconColor = new(1.0f, 0.6f, 0.2f);

    private readonly RemoteSession _session;
    private readonly GameplayWorld _world;
    private readonly RemoteControl _control;
    private readonly int _steps;
    private readonly string _captureDir;
    private readonly FailureLog _failures = new();
    private Subscription? _events;
    private FrameReport? _baseline;
    private string _baselineImage = "";

    public Demo(RemoteSession session, int steps, string captureDir)
    {
        _session = session;
        _world = new GameplayWorld(session);
        _control = session.Control;
        _steps = steps;
        _captureDir = captureDir;
    }

    // The square trajectory: p = 4k/K walks the four edges once.
    private Vector3 PositionAt(int k)
    {
        float p = 4.0f * k / _steps;
        int whole = (int)p;
        int side = whole % 4;
        float u = p - whole;
        Vector3 a = Corners[side];
        Vector3 b = Corners[(side + 1) % 4];
        return new Vector3(a.X + (b.X - a.X) * u, a.Y + (b.Y - a.Y) * u, a.Z + (b.Z - a.Z) * u);
    }

    private float IntensityAt(int k)
    {
        float p = 4.0f * k / _steps;
        float u = p - (int)p;
        return 4.0f + 2.0f * (1.0f - MathF.Abs(2.0f * u - 1.0f));
    }

    private static string Number(float value) => value.ToString("G6", CultureInfo.InvariantCulture);

    private static string Position(Vector3 at) => Number(at.X) + " " + Number(at.Y) + " " + Number(at.Z);

    private static void Line(string step, string text) => Console.WriteLine(step + ": " + text);

    public int Run()
    {
        try
        {
            Connect();
            Find();
            Baseline();
            Spawn();
            for (int k = 1; k <= _steps; ++k) Move(k);
            Refuse();
            Capture();
            Destroy();
        }
        catch (StepFailed failed)
        {
            Console.WriteLine(failed.Step + ": FAILED " + failed.Message);
            TryResume();
            return failed.Code;
        }
        catch (GameplayException e)
        {
            Console.WriteLine("FAILED " + e.Message);
            TryResume();
            return 3;
        }
        _control.Resume();
        Line("9 resume", "ok");
        return 0;
    }

    private void TryResume()
    {
        try
        {
            if (_session.Failure is null) _control.Resume();
        }
        catch (RemoteException)
        {
        }
    }

    // A step's work; an SDK exception becomes this step's failure.
    private static void Guard(string step, Action action)
    {
        try
        {
            action();
        }
        catch (RemoteException e)
        {
            throw new StepFailed(step, e.Error.ToString(), 3);
        }
        catch (SchemaMismatchException e)
        {
            throw new StepFailed(step, e.Message, 3);
        }
        catch (GameplayException e)
        {
            throw new StepFailed(step, e.Message, 1);
        }
    }

    private static Exception Fail(string step, string why) => new StepFailed(step, why, 1);

    // One stepped frame (paused: exactly one frame of command application), its report.
    private FrameReport StepOnce(string step, string? image = null)
    {
        StepResult result = _control.Step(new StepRequest(1, image));
        return result.Report ?? throw Fail(step, "step refused: " + result.Error);
    }

    private static FramePointLight? BeaconIn(FrameReport report, Vector3 at) =>
        report.Data.PointLights.FirstOrDefault(light => light.Position == at);

    // 1. The bindings this client uses match the Runtime's schema (R4).
    private void Connect() => Guard("1 connect", () =>
    {
        _world.CheckCompatible<W.Light>();
        _world.CheckCompatible<W.WorldMatrix>();
        Line("1 connect", "ok (bindings Light, LightKind, WorldMatrix match the Runtime's schema)");
    });

    // 2. Find the Directional light through a typed query.
    private void Find() => Guard("2 find", () =>
    {
        if (_world.Exists(Beacon)) throw Fail("2 find", "a beacon from an earlier run is still there: " + Beacon);
        IReadOnlyList<EntityGuid> lights = _world.EntitiesWith<W.Light, W.WorldMatrix>();
        int directional = 0;
        float sunIntensity = 0.0f;
        foreach (EntityGuid entity in lights)
        {
            W.Light light = _world.Get<W.Light>(entity);
            if (light.Kind != W.LightKind.Directional) continue;
            ++directional;
            sunIntensity = light.Intensity;
        }
        if (directional != 1) throw Fail("2 find", "expected exactly one Directional light, found " + directional);
        Line("2 find", "ok (" + lights.Count + " light(s); the Directional light's intensity is " + Number(sunIntensity) + ")");
    });

    // 3. Pause; the baseline frame and image.
    private void Baseline() => Guard("3 baseline", () =>
    {
        _control.Pause();
        _baselineImage = Path.Combine(_captureDir, "atlantis_csharp_gameplay_demo_baseline.png");
        FrameReport report = StepOnce("3 baseline", _baselineImage);
        if (report.Image is null || !File.Exists(_baselineImage)) throw Fail("3 baseline", "no baseline image");
        _baseline = report;
        Line("3 baseline", "ok (paused; " + report.Data.PointLights.Count + " point light(s))");
    });

    // 4. Spawn the beacon: one typed transaction, the Light (Point) before the WorldMatrix.
    private void Spawn() => Guard("4 spawn", () =>
    {
        _events = _world.Subscribe(new EventFilter(EventKind.All, Beacon));
        Vector3 at = PositionAt(0);
        TransactionTicket ticket = _world.Submit(new Transaction()
            .Create(Beacon)
            .Add(Beacon, new W.Light { Kind = W.LightKind.Point, Color = BeaconColor, Intensity = IntensityAt(0), Range = 4.0f })
            .Add(Beacon, new W.WorldMatrix
            {
                Column0 = new Vector4(1.0f, 0.0f, 0.0f, 0.0f), Column1 = new Vector4(0.0f, 1.0f, 0.0f, 0.0f),
                Column2 = new Vector4(0.0f, 0.0f, 1.0f, 0.0f), Column3 = new Vector4(at, 1.0f),
            }));
        FrameReport report = StepOnce("4 spawn");
        _failures.Absorb(_world.DrainFailures());
        if (_failures.Refusal(ticket) is CommandFailure refusal) throw Fail("4 spawn", "refused: " + refusal.Error);
        if (report.Data.PointLights.Count != _baseline!.Data.PointLights.Count + 1)
        {
            throw Fail("4 spawn", "expected one more point light in the frame");
        }
        FramePointLight? light = BeaconIn(report, at);
        if (light is null || light.Color != BeaconColor || light.Intensity != IntensityAt(0) || light.Range != 4.0f)
        {
            throw Fail("4 spawn", "the frame's beacon is not exactly what was written");
        }
        IReadOnlyList<Event> events = _events.Drain();
        if ((ulong)events.Count != ticket.Count || events[0] != new EntityCreated(Beacon))
        {
            throw Fail("4 spawn", "expected one event per command, EntityCreated first");
        }
        Line("4 spawn", "ok (" + ticket.Count + " commands in one transaction; the next frame has the beacon at " + Position(at) + ")");
    });

    // 5. One logic step: move and pulse, one transaction, one frame, exact.
    private void Move(int k)
    {
        string step = "5." + k + " move";
        Guard(step, () =>
        {
            Vector3 at = PositionAt(k);
            float intensity = IntensityAt(k);
            _world.Submit(new Transaction()
                .Set(Beacon, W.Fields.WorldMatrix.Column3, new Vector4(at, 1.0f))
                .Set(Beacon, W.Fields.Light.Intensity, intensity));
            FrameReport report = StepOnce(step);
            FramePointLight? light = BeaconIn(report, at);
            if (light is null || light.Intensity != intensity)
            {
                throw Fail(step, "the stepped frame does not show the values written for this step");
            }
            IReadOnlyList<Event> events = _events!.Drain();
            if (events.Count != 2) throw Fail(step, "expected two PropertyChanged events");
            if (!_world.TryDecode(events[1], W.Fields.Light.Intensity, out float decoded) || decoded != intensity)
            {
                throw Fail(step, "the typed event decode does not match");
            }
            Line(step, "ok (position " + Position(at) + ", intensity " + Number(intensity) + ")");
        });
    }

    // 6. A refused transaction changes nothing: one failure by ticket, and its valid half did not apply either.
    private void Refuse() => Guard("6 refuse", () =>
    {
        TransactionTicket ticket = _world.Submit(new Transaction()
            .Set(Beacon, W.Fields.Light.Intensity, 9.0f)
            .Set(Beacon, W.Fields.Light.Color, new Vector3(float.NaN, 0.0f, 0.0f)));
        FrameReport report = StepOnce("6 refuse");
        _failures.Absorb(_world.DrainFailures());
        if (_failures.Refusal(ticket) is not CommandFailure refusal || refusal.Error != AccessError.NonFiniteValue)
        {
            throw Fail("6 refuse", "expected one NonFiniteValue failure inside the transaction's tickets");
        }
        FramePointLight? light = BeaconIn(report, PositionAt(_steps));
        if (light is null || light.Intensity != IntensityAt(_steps))
        {
            throw Fail("6 refuse", "the frame changed: the transaction was not all or nothing");
        }
        if (_events!.Drain().Count != 0) throw Fail("6 refuse", "an aborted transaction emitted events");
        Line("6 refuse", "ok (" + refusal.Error + " at command " + (refusal.Ticket.Value - ticket.First.Value + 1) + " of " +
                         ticket.Count + "; nothing applied)");
    });

    // 7. Capture: the image differs from the baseline (PNG bytes).
    private void Capture() => Guard("7 capture", () =>
    {
        string image = Path.Combine(_captureDir, "atlantis_csharp_gameplay_demo_beacon.png");
        FrameReport report = StepOnce("7 capture", image);
        if (report.Image is null || !File.Exists(image)) throw Fail("7 capture", "no image");
        if (File.ReadAllBytes(image).AsSpan().SequenceEqual(File.ReadAllBytes(_baselineImage)))
        {
            throw Fail("7 capture", "the image equals the baseline");
        }
        Line("7 capture", "ok (" + report.Image.Width + "x" + report.Image.Height + "; differs from the baseline)");
    });

    // 8. Destroy: the frame returns to the original lights.
    private void Destroy() => Guard("8 destroy", () =>
    {
        _world.Submit(new Transaction().Destroy(Beacon));
        FrameReport report = StepOnce("8 destroy");
        if (!report.Data.PointLights.SequenceEqual(_baseline!.Data.PointLights) ||
            !report.Data.DirectionalLights.SequenceEqual(_baseline.Data.DirectionalLights))
        {
            throw Fail("8 destroy", "the frame's lights are not the original ones");
        }
        IReadOnlyList<Event> events = _events!.Drain();
        if (events.Count == 0 || events[^1] != new EntityDestroyed(Beacon)) throw Fail("8 destroy", "expected EntityDestroyed");
        if (_world.Exists(Beacon)) throw Fail("8 destroy", "the beacon still exists");
        _events.Dispose();
        _events = null;
        Line("8 destroy", "ok (the frame's lights equal the baseline's)");
    });
}
