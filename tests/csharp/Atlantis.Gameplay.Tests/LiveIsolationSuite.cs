// Plan 0058 P9 (Spec 0058 R3; Spec 0057 R10's read contract over C#): the
// "live.isolation" suite -- two sessions on one fixture server. A multi-leaf
// read is not a snapshot: when session B's step completes between session
// A's leaves, A's read mixes values from before and after it. A read is
// consistent while the Runtime stays paused, nobody steps or resumes it, and
// no step is pending: B's commands are then held, and A's reads agree.
// The order is made by waiting for B's step to complete, never by timing.
using Atlantis.Gameplay.Remote;
using static Atlantis.Gameplay.Tests.LiveConnectionSuite;
using W = Atlantis.Gameplay.World;

namespace Atlantis.Gameplay.Tests;

public static class LiveIsolationSuite
{
    public static void Run(TestContext t)
    {
        using var server = new FixtureServer(t);
        using RemoteSession a = server.Attach();
        using RemoteSession b = server.Attach();
        var readerWorld = new GameplayWorld(a);
        var writerWorld = new GameplayWorld(b);

        b.Control.Pause();
        Settle(a);
        W.Light before = readerWorld.Get<W.Light>(Sun);

        // B changes two leaves; paused, they are held.
        writerWorld.Submit(new Transaction()
            .Set(Sun, W.Fields.Light.Intensity, 7.0f)
            .Set(Sun, W.Fields.Light.Range, 8.0f));
        Settle(a, 4);
        t.Equal(before, readerWorld.Get<W.Light>(Sun), "paused, no step: B's commands are held, and A's read is consistent");
        t.Equal(before, readerWorld.Get<W.Light>(Sun), "... and repeatable");

        // The mixed read: A reads intensity, B's step completes, A reads range.
        float intensity = readerWorld.Get(Sun, W.Fields.Light.Intensity);
        StepResult step = b.Control.Step(new StepRequest(1));
        t.Check(step.IsOk, "B's step completed between A's leaves");
        float range = readerWorld.Get(Sun, W.Fields.Light.Range);
        t.Check(intensity == before.Intensity && range == 8.0f,
                "a read across a completed step mixes leaves from before (intensity " + intensity + ") and after (range " + range + ")");

        // Paused again with nothing pending: consistent, and showing the step's effects.
        W.Light after = readerWorld.Get<W.Light>(Sun);
        t.Check(after.Intensity == 7.0f && after.Range == 8.0f && after == readerWorld.Get<W.Light>(Sun),
                "after the step, paused and nothing pending: a consistent read of both changes");

        b.Control.Resume();
        t.Check(!a.Control.Status().Paused, "the pause is the Runtime's, not a session's: A sees B's resume");
    }
}
