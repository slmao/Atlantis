// Plan 0058 P9 (Spec 0058 R4; ADR-0112 D3): the "live.compat" suite -- one
// fixture server per --mutate schema. The mutated type is refused by every
// typed call, recursively (a change only to a referenced enum or nested
// struct refuses its component); unrelated types and the reflective layer
// still work; a transaction touching the mismatch sends nothing.
using System;
using System.Numerics;
using Atlantis.Gameplay.Remote;
using static Atlantis.Gameplay.Tests.LiveConnectionSuite;
using W = Atlantis.Gameplay.World;

namespace Atlantis.Gameplay.Tests;

public static class LiveCompatSuite
{
    public static void Run(TestContext t)
    {
        foreach (string mutation in new[] { "light-version", "light-kind-constant", "light-intensity-readonly", "light-extra-field" })
        {
            using var server = new FixtureServer(t, mutate: mutation, record: true);
            using RemoteSession session = server.Attach();
            var world = new GameplayWorld(session);
            var record = new RecordFile(server.RecordPath!);
            Mismatch(t, mutation, "world::Light", () => world.Get(Sun, W.Fields.Light.Intensity));
            Mismatch(t, mutation, "world::Light", () => world.Get<W.Light>(Sun));
            Mismatch(t, mutation, "world::Light", () => world.Set(Sun, W.Fields.Light.Intensity, 2.0f));
            Mismatch(t, mutation, "world::Light", () => world.EntitiesWith<W.Transform, W.Light>());
            Mismatch(t, mutation, "world::Light",
                     () => world.TryDecode(new PropertyChanged(world.Resolve(Sun, "Light.intensity"), 2.0f), W.Fields.Light.Intensity, out _));
            Mismatch(t, mutation, "world::Light", () => world.Submit(new Transaction()
                .Set(Sun, W.Fields.WorldMatrix.Column3, new Vector4(0.0f, 1.0f, 0.0f, 1.0f))  // compatible
                .Set(Sun, "Transform.localScale", new Vector3(2.0f, 2.0f, 2.0f))             // reflective
                .Set(Sun, W.Fields.Light.Intensity, 2.0f)));                                  // incompatible
            t.Equal(0, record.Count(), mutation + ": a mixed transaction with the mismatch sends nothing");
            t.Check(world.Get<W.Transform>(Sun).LocalScale == Vector3.One, mutation + ": an unrelated type still reads");
            t.Equal(new Vector4(0, 5, 0, 1), world.Get(Sun, W.Fields.WorldMatrix.Column3), mutation + ": ... and its fields");
            t.Equal(PropertyValue.FromFloat32(1.2f), world.Get(Sun, "Light.intensity"), mutation + ": the reflective layer is not blocked");
        }

        using (var server = new FixtureServer(t, mutate: "camera-fog-version"))
        using (RemoteSession session = server.Attach())
        {
            var world = new GameplayWorld(session);
            Mismatch(t, "camera-fog-version", "world::Camera", () => world.Get<W.Camera>(Camera));
            Mismatch(t, "camera-fog-version", "world::Camera", () => world.Get(Camera, W.Fields.Camera.NearZ));
            t.Equal(1.2f, world.Get(Sun, W.Fields.Light.Intensity), "camera-fog-version: Light is unaffected");
        }

        using (var server = new FixtureServer(t))
        using (RemoteSession session = server.Attach())
        {
            var world = new GameplayWorld(session);
            world.CheckCompatible<W.Light>();
            world.CheckCompatible<W.Camera>();
            world.CheckCompatible<W.Renderable>();
            world.CheckCompatible<W.Transform>();
            world.CheckCompatible<W.WorldMatrix>();
            t.Check(true, "the unmutated schema: every World component is compatible");
        }
    }

    private static void Mismatch(TestContext t, string mutation, string subject, Action action) =>
        t.Equal(subject, t.Throws<SchemaMismatchException>(action, mutation + ": refused")?.Subject, mutation + ": names " + subject);
}
