// Plan 0058 P9 (Spec 0058 R3; ruling Q5, J7): the "live.reflective" suite,
// against a recording fixture server -- get and set by path and id,
// ReadComponent, EntitiesWith, transactions all or nothing, name errors that
// send nothing, refusals (a query's thrown, a command's by ticket),
// subscriptions released on Dispose.
using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using static Atlantis.Gameplay.Tests.LiveConnectionSuite;

namespace Atlantis.Gameplay.Tests;

public static class LiveReflectiveSuite
{
    public static void Run(TestContext t)
    {
        using var server = new FixtureServer(t, record: true);
        using Remote.RemoteSession session = server.Attach();
        var world = new GameplayWorld(session);
        var record = new RecordFile(server.RecordPath!);

        // Get and set.
        t.Equal(PropertyValue.FromFloat32(1.2f), world.Get(Sun, "Light.intensity"), "Get by short path");
        t.Equal(PropertyValue.FromFloat32(1.2f), world.Get(Sun, "world::Light.intensity"), "Get by qualified path");
        PropertyAddress address = world.Resolve(Sun, "Light.intensity");
        t.Equal(PropertyValue.FromFloat32(1.2f), world.Get(address), "Get by address");
        t.Equal(PropertyKind.Float32, world.Get(Camera, "Camera.fog.density").Kind, "a nested leaf");
        CommandTicket ticket = world.Set(Sun, "Light.intensity", 2.5f);
        t.Check(ticket.Value > 0 && record.Take() == "{\"transaction\":false,\"commands\":[" +
                "{\"kind\":\"SetProperty\",\"address\":{\"entity\":\"52052052-0002-4052-8052-000000000002\",\"component\":\"0x8325934757106b8d\"," +
                "\"field\":\"0x000145df4f8a3cee\"},\"value\":{\"f32\":2.5}}]}", "Set: one SetProperty, submitted alone");
        Settle(session);
        t.Equal(PropertyValue.FromFloat32(2.5f), world.Get(Sun, "Light.intensity"), "... applied at the next frame");

        // Name errors are the client's own and send nothing.
        NameIs(t, NameError.UnknownType, () => world.Get(Sun, "Lantern.intensity"), "an unknown type");
        NameIs(t, NameError.UnknownField, () => world.Get(Sun, "Light.brightness"), "an unknown field");
        NameIs(t, NameError.UnknownField, () => world.Get(Sun, "Light.intensity.x"), "a path below a leaf");
        NameIs(t, NameError.NotALeaf, () => world.Get(Camera, "Camera.fog"), "a path that stops at a struct");
        NameIs(t, NameError.NotALeaf, () => world.Get(Sun, "Light"), "a type alone");
        NameIs(t, NameError.UnknownType, () => world.Set(Sun, "Lantern.intensity", 1.0f), "Set with an unknown type");
        t.Equal(0, record.Count(), "name errors sent nothing");

        // A query's refusal is thrown (J7); a command's comes back by ticket.
        t.Equal(AccessError.ComponentMissing, t.Throws<RefusedException>(() => world.Get(Empty, "Light.intensity"),
                "Get without the component throws")?.Error, "... RefusedException(ComponentMissing)");
        t.Equal(AccessError.UnknownEntity, t.Throws<RefusedException>(() => world.Components(Unknown),
                "Components of an unknown entity throws")?.Error, "... RefusedException(UnknownEntity)");
        CommandTicket refused = world.Set(Sun, "Light.intensity", float.NaN);
        Settle(session);
        var log = new FailureLog();
        log.Absorb(world.DrainFailures());
        t.Equal(new CommandFailure(refused, AccessError.NonFiniteValue), log.Refusal(refused),
                "a command's refusal is a value, by ticket: NonFiniteValue (the SDK adds no pre-check)");
        record.Take();

        // ReadComponent.
        DynamicComponent light = world.ReadComponent(Sun, "Light");
        t.Check(light.Name == "world::Light" && light.Leaves.Select(l => l.Path).SequenceEqual(
                    new[] { "Light.kind", "Light.color", "Light.intensity", "Light.range" }),
                "ReadComponent: every leaf, in leaf order");
        t.Equal(PropertyValue.FromFloat32(2.5f), light.Leaves[2].Value, "... with values");
        t.Equal(11, world.ReadComponent(Camera, "world::Camera").Leaves.Count, "ReadComponent of Camera: nested leaves expanded");
        t.Throws<RefusedException>(() => world.ReadComponent(Empty, "Light"), "ReadComponent without the component throws");
        NameIs(t, NameError.UnknownType, () => world.ReadComponent(Sun, "LightKind"), "ReadComponent of an enum");

        // EntitiesWith.
        t.Equal(string.Join(",", Sun, Lamp), string.Join(",", world.EntitiesWith("Light")), "EntitiesWith(Light)");
        t.Equal(string.Join(",", Sun, Lamp), string.Join(",", world.EntitiesWith("Light", "world::Transform")),
                "EntitiesWith(Light, Transform)");
        t.Equal(Mesh.ToString(), string.Join(",", world.EntitiesWith("Renderable")), "EntitiesWith(Renderable)");
        t.Equal(5, world.EntitiesWith(Array.Empty<string>()).Count, "EntitiesWith of no type is every entity");
        NameIs(t, NameError.UnknownType, () => world.EntitiesWith("Light", "Lantern"), "EntitiesWith an unknown type");

        // Transactions: one submitTransaction of the operations, in order; all or nothing.
        EntityGuid e = EntityGuid.Parse("58005800-0000-4000-8000-0000000000d1");
        TransactionTicket created = world.Submit(new Transaction()
            .Create(e)
            .Add(e, "Light", new (string, PropertyValue)[] { ("kind", PropertyValue.FromEnum(1)), ("intensity", 4.0f) })
            .Add(e, "Transform")
            .Set(e, "Transform.localScale", new Vector3(2, 2, 2)));
        t.Equal(6UL, created.Count, "a transaction: one ticket per operation");
        Settle(session);
        t.Check(world.Exists(e) && world.Get(e, "Light.kind") == PropertyValue.FromEnum(1) &&
                world.Get(e, "Light.intensity") == PropertyValue.FromFloat32(4) &&
                world.Get(e, "Transform.localScale") == PropertyValue.FromVec3(new Vector3(2, 2, 2)), "... applied in order");
        record.Take();
        TransactionTicket aborted = world.Submit(new Transaction()
            .Set(e, "Light.intensity", 9.0f)
            .Add(e, "Light")
            .Set(e, "Light.range", 9.0f));
        Settle(session);
        log.Absorb(world.DrainFailures());
        t.Equal(AccessError.ComponentAlreadyPresent, log.Refusal(aborted)?.Error, "a transaction is refused whole: one failure in its range");
        t.Equal(PropertyValue.FromFloat32(4), world.Get(e, "Light.intensity"), "... and none of it applied");
        record.Take();
        NameIs(t, NameError.UnknownField, () => world.Submit(new Transaction().Destroy(e).Set(e, "Light.brightness", 1.0f)),
               "a transaction with an unresolvable name");
        t.Equal(0, record.Count(), "... is refused whole: nothing sent, no ticket");
        world.Submit(new Transaction().Remove(e, "Transform").Remove(e, new TypeId(0x8325934757106b8dUL)).Destroy(e));
        Settle(session);
        t.Check(!world.Exists(e), "remove by name and by id, destroy");

        // Subscriptions.
        Subscription subscription = world.Subscribe(new EventFilter(EventKind.PropertyChanged, Lamp));
        world.Set(Lamp, "Light.range", 3.5f);
        world.Set(Sun, "Light.range", 3.5f);
        Settle(session);
        IReadOnlyList<Event> events = subscription.Drain();
        t.Check(events.Count == 1 && events[0] is PropertyChanged { Value: var v } && v == PropertyValue.FromFloat32(3.5f),
                "a subscription drains its filter's events");
        SubscriptionId id = subscription.Id;
        subscription.Dispose();
        t.Equal(ConnectionError.UnknownSubscription, session.Unsubscribe(id), "Dispose unsubscribed it");
        t.Throws<ConnectionRefusedException>(() => subscription.Drain(), "a disposed subscription does not drain");
    }

    private static void NameIs(TestContext t, NameError expected, Action action, string what) =>
        t.Equal(expected, t.Throws<UnknownNameException>(action, what + " throws")?.Error, what + ": " + expected);
}

/// <summary>The fixture server's --record file: one line per submission, taken in order.</summary>
public sealed class RecordFile
{
    private readonly string _path;
    private int _taken;

    public RecordFile(string path) => _path = path;

    private string[] Lines()
    {
        using var stream = new System.IO.FileStream(_path, System.IO.FileMode.Open, System.IO.FileAccess.Read,
                                                    System.IO.FileShare.ReadWrite | System.IO.FileShare.Delete);
        using var reader = new System.IO.StreamReader(stream);
        string text = reader.ReadToEnd();
        return text.Length == 0 ? Array.Empty<string>() : text.TrimEnd('\n').Split('\n');
    }

    /// <summary>How many submissions were recorded since the last take.</summary>
    public int Count() => Lines().Length - _taken;

    /// <summary>The submissions since the last take, one per line, joined by '\n'.</summary>
    public string Take()
    {
        string[] lines = Lines();
        string taken = string.Join("\n", lines.Skip(_taken));
        _taken = lines.Length;
        return taken;
    }
}
