// Plan 0058 P9 (Spec 0058 R3; ADR-0112 D3, D5; J6): the "live.typed" suite,
// against a recording fixture server -- Get<T>, field Get and Set, the
// valued and the bare Add, Remove, TryDecode, typed EntitiesWith, value
// initialization's zeros against the World's defaults, and parity: every
// scenario of tests/csharp/conformance/sdk_transcripts.jsonl, run by the C#
// SDK, submits exactly the commands the C++ SDK submitted.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Numerics;
using Atlantis.Gameplay.Remote;
using static Atlantis.Gameplay.Tests.LiveConnectionSuite;
using W = Atlantis.Gameplay.World;

namespace Atlantis.Gameplay.Tests;

public static class LiveTypedSuite
{
    private static readonly EntityGuid Beacon = EntityGuid.Parse("58005800-0000-4000-8000-0000000000b1");

    public static void Run(TestContext t)
    {
        Typed(t);
        Parity(t);
    }

    private static void Typed(TestContext t)
    {
        using var server = new FixtureServer(t, record: true);
        using RemoteSession session = server.Attach();
        var world = new GameplayWorld(session);
        var record = new RecordFile(server.RecordPath!);

        W.Light sun = world.Get<W.Light>(Sun);
        t.Check(sun.Kind == W.LightKind.Directional && sun.Color == new Vector3(0.6f, 0.7f, 1.0f) && sun.Intensity == 1.2f,
                "Get<Light>: every leaf, typed");
        DynamicComponent reflective = world.ReadComponent(Sun, "Light");
        t.Check(reflective.Leaves[3].Value == PropertyValue.FromFloat32(sun.Range), "... equal to ReadComponent's values");
        W.Camera camera = world.Get<W.Camera>(Camera);
        t.Equal(world.Get(Camera, "Camera.fog.density").AsFloat32(), camera.Fog.Density, "Get<Camera>: nested structs filled");
        t.Equal(1.2f, world.Get(Sun, W.Fields.Light.Intensity), "Get(field): the leaf's C# type");
        t.Equal(W.LightKind.Point, world.Get(Lamp, W.Fields.Light.Kind), "Get(field): an enum");
        t.Equal(camera.Bloom.Threshold, world.Get(Camera, W.Fields.Camera.Bloom.Threshold), "Get(field): a nested leaf");
        t.Equal(AccessError.ComponentMissing, t.Throws<RefusedException>(() => world.Get<W.Light>(Empty),
                "Get<Light> without the component throws")?.Error, "... RefusedException(ComponentMissing)");

        world.Set(Sun, W.Fields.Light.Intensity, 2.0f);
        string typedSet = record.Take();
        world.Set(Sun, "Light.intensity", 2.0f);
        t.Equal(typedSet, record.Take(), "a typed Set sends the reflective Set's command");
        Settle(session);
        t.Equal(2.0f, world.Get(Sun, W.Fields.Light.Intensity), "... applied");

        t.Equal(string.Join(",", world.EntitiesWith("Light")), string.Join(",", world.EntitiesWith<W.Light>()),
                "EntitiesWith<Light> equals the reflective filter");
        t.Equal(Mesh.ToString(), string.Join(",", world.EntitiesWith<W.Renderable, W.Transform, W.WorldMatrix>()),
                "EntitiesWith<Renderable, Transform, WorldMatrix>");
        t.Equal(Camera.ToString(), string.Join(",", world.EntitiesWith<W.Camera, W.Transform>()), "EntitiesWith<Camera, Transform>");

        // Value initialization writes zeros and enum value 0; the bare Add gives the World's defaults.
        EntityGuid zeros = EntityGuid.Parse("58005800-0000-4000-8000-0000000000e1");
        EntityGuid defaults = EntityGuid.Parse("58005800-0000-4000-8000-0000000000e2");
        var beacon = new W.Light { Kind = W.LightKind.Point, Color = new Vector3(1.0f, 0.6f, 0.2f), Intensity = 4.0f, Range = 4.0f };
        world.Submit(new Transaction().Create(Beacon).Add(Beacon, beacon).Create(zeros).Add(zeros, default(W.Light))
                         .Create(defaults).Add<W.Light>(defaults));
        Settle(session);
        t.Check(world.DrainFailures().Count == 0, "valued, zero and bare adds applied");
        t.Equal(beacon, world.Get<W.Light>(Beacon), "Add(entity, value): read back equal");
        W.Light zero = world.Get<W.Light>(zeros);
        t.Check(zero.Kind == W.LightKind.Directional && zero.Color == Vector3.Zero && zero.Intensity == 0 && zero.Range == 0,
                "Add(entity, default(Light)): zeros and enum value 0, not the World's defaults");
        W.Light worldDefault = world.Get<W.Light>(defaults);
        t.Check(worldDefault.Color == Vector3.One && worldDefault.Intensity == 1.0f, "Add<Light>(entity): the World's defaults");

        // TryDecode.
        using (Subscription events = world.Subscribe(new EventFilter(EventKind.PropertyChanged, Beacon)))
        {
            world.Set(Beacon, W.Fields.Light.Intensity, 6.0f);
            world.Set(Beacon, W.Fields.Light.Color, new Vector3(0.5f, 0.5f, 0.5f));
            Settle(session);
            IReadOnlyList<Event> changed = events.Drain();
            t.Check(changed.Count == 2 && world.TryDecode(changed[0], W.Fields.Light.Intensity, out float intensity) && intensity == 6.0f,
                    "TryDecode: the handle's value from its PropertyChanged");
            t.Check(!world.TryDecode(changed[1], W.Fields.Light.Intensity, out _) &&
                    world.TryDecode(changed[1], W.Fields.Light.Color, out Vector3 color) && color == new Vector3(0.5f),
                    "... false for another property's event");
            t.Check(!world.TryDecode(new EntityCreated(Beacon), W.Fields.Light.Intensity, out _), "... false for another kind");
        }

        world.Submit(new Transaction().Remove<W.Light>(Beacon).Destroy(Beacon).Destroy(zeros).Destroy(defaults));
        Settle(session);
        t.Check(!world.Exists(Beacon) && !world.Exists(zeros) && !world.Exists(defaults), "Remove<Light>, destroy");
    }

    // The P3 scenarios, with the values the C++ scenarios used.
    private static readonly (string Name, Action<GameplayWorld> Run)[] Scenarios =
    {
        ("typed.set.intensity", world => world.Set(Sun, W.Fields.Light.Intensity, 2.5f)),
        ("reflective.set.path", world => world.Set(Sun, "Light.intensity", 2.5f)),
        ("typed.add.light", world => world.Submit(new Transaction().Create(Beacon).Add(Beacon,
            new W.Light { Kind = W.LightKind.Point, Color = new Vector3(1.0f, 0.6f, 0.2f), Intensity = 4.0f, Range = 4.0f }))),
        ("typed.spawn.beacon", world => world.Submit(new Transaction()
            .Create(Beacon)
            .Add(Beacon, new W.Light { Kind = W.LightKind.Point, Color = new Vector3(1.0f, 0.6f, 0.2f), Intensity = 4.0f, Range = 4.0f })
            .Add(Beacon, new W.WorldMatrix
            {
                Column0 = new Vector4(1, 0, 0, 0), Column1 = new Vector4(0, 1, 0, 0), Column2 = new Vector4(0, 0, 1, 0),
                Column3 = new Vector4(1.5f, 2.0f, 1.5f, 1.0f),
            }))),
        ("typed.move", world => world.Submit(new Transaction()
            .Set(Beacon, W.Fields.WorldMatrix.Column3, new Vector4(0.0f, 2.0f, 1.5f, 1.0f))
            .Set(Beacon, W.Fields.Light.Intensity, 6.0f))),
        ("reflective.add.valued", world => world.Submit(new Transaction()
            .Add(Beacon, "Light", new (string, PropertyValue)[] { ("kind", PropertyValue.FromEnum(1)), ("intensity", 4.0f) }))),
        ("mixed.transaction", world => world.Submit(new Transaction()
            .Set(Beacon, W.Fields.WorldMatrix.Column3, new Vector4(0.0f, 1.0f, 0.0f, 1.0f))
            .Set(Beacon, "Transform.localScale", new Vector3(2.0f, 2.0f, 2.0f)))),
        ("bare.add.remove.destroy", world => world.Submit(new Transaction()
            .Add<W.Light>(Beacon).Remove<W.WorldMatrix>(Beacon).Destroy(Beacon))),
        ("typed.refused.nan", world => world.Submit(new Transaction()
            .Set(Beacon, W.Fields.Light.Intensity, 9.0f)
            .Set(Beacon, W.Fields.Light.Color, new Vector3(float.NaN, 0.0f, 0.0f)))),
    };

    private static void Parity(TestContext t)
    {
        string path = t.RepoPath("tests", "csharp", "conformance", "sdk_transcripts.jsonl");
        var expected = new Dictionary<string, string>();
        foreach (string line in File.ReadAllText(path).TrimEnd('\n').Split('\n'))
        {
            expected[Json.Parse(line).Find("scenario")!.AsString] = line;
        }
        t.Equal(expected.Count, Scenarios.Length, "parity: every committed scenario has a C# run");

        using var server = new FixtureServer(t, record: true);
        using RemoteSession session = server.Attach();
        var world = new GameplayWorld(session);
        var record = new RecordFile(server.RecordPath!);
        foreach ((string name, Action<GameplayWorld> run) in Scenarios)
        {
            run(world);
            string submissions = string.Join(",", record.Take().Split('\n', StringSplitOptions.RemoveEmptyEntries));
            string actual = "{\"scenario\":\"" + name + "\",\"submissions\":[" + submissions + "]}";
            t.Check(expected.TryGetValue(name, out string? line) && line == actual,
                    "parity: " + name + " submits what the C++ SDK submitted" + (line == actual ? "" : " -- got " + actual));
        }
    }
}
