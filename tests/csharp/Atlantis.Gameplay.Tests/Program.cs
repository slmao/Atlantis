// Plan 0058 P9: runs the suite named on the command line and returns 0 when
// every check passed, 1 otherwise; each check prints its name.
//
//   Atlantis.Gameplay.Tests <suite> --repo <source dir> [--fixture-server <exe>]
using System;
using System.Collections.Generic;

namespace Atlantis.Gameplay.Tests;

public static class Program
{
    private static readonly Dictionary<string, Action<TestContext>> Suites = new()
    {
        ["codec"] = CodecSuite.Run,
        ["session"] = SessionSuite.Run,
        ["live.connection"] = LiveConnectionSuite.Run,
        ["generated"] = GeneratedSuite.Run,
        ["live.reflective"] = LiveReflectiveSuite.Run,
        ["live.typed"] = LiveTypedSuite.Run,
        ["live.compat"] = LiveCompatSuite.Run,
        ["live.isolation"] = LiveIsolationSuite.Run,
    };

    public static int Main(string[] args)
    {
        if (args.Length < 1 || !Suites.TryGetValue(args[0], out Action<TestContext>? suite))
        {
            Console.Error.WriteLine("usage: Atlantis.Gameplay.Tests <" + string.Join("|", Suites.Keys) +
                                    "> --repo <dir> [--fixture-server <exe>]");
            return 2;
        }
        var context = new TestContext(args);
        try
        {
            suite(context);
        }
        catch (Exception e)
        {
            context.Fail("an unexpected " + e.GetType().Name + ": " + e);
        }
        Console.WriteLine($"{args[0]}: {context.Passed} checks passed, {context.Failed} failed");
        return context.Failed == 0 ? 0 : 1;
    }
}
