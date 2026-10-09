// Plan 0058 P9: the test program's checks and its arguments. A failed check
// is printed and counted, and the suite goes on; Require throws to stop it.
using System;
using System.IO;

namespace Atlantis.Gameplay.Tests;

public sealed class AssertionFailed : Exception
{
    public AssertionFailed(string message) : base(message) { }
}

public sealed class TestContext
{
    public TestContext(string[] args)
    {
        for (int i = 1; i + 1 < args.Length; i += 2)
        {
            switch (args[i])
            {
                case "--repo": Repo = args[i + 1]; break;
                case "--fixture-server": FixtureServer = args[i + 1]; break;
                default: throw new ArgumentException("unknown argument " + args[i]);
            }
        }
    }

    public string Repo { get; } = ".";
    public string? FixtureServer { get; }
    public int Passed { get; private set; }
    public int Failed { get; private set; }

    public string RepoPath(params string[] parts) => Path.Combine(Repo, Path.Combine(parts));

    public void Check(bool condition, string name)
    {
        if (condition)
        {
            ++Passed;
            Console.WriteLine("  ok    " + name);
        }
        else
        {
            Fail(name);
        }
    }

    public void Equal<T>(T expected, T actual, string name)
    {
        bool same = Equals(expected, actual);
        Check(same, same ? name : name + " -- expected <" + expected + ">, got <" + actual + ">");
    }

    public void Equal<T>(T expected, T actual, string name, System.Collections.Generic.IEqualityComparer<T> comparer)
    {
        bool same = comparer.Equals(expected, actual);
        Check(same, same ? name : name + " -- expected <" + expected + ">, got <" + actual + ">");
    }

    public void Require(bool condition, string name)
    {
        Check(condition, name);
        if (!condition) throw new AssertionFailed(name);
    }

    /// <summary>Passes when <paramref name="action"/> throws <typeparamref name="TException"/>; returns it.</summary>
    public TException? Throws<TException>(Action action, string name) where TException : Exception
    {
        try
        {
            action();
        }
        catch (TException e)
        {
            Check(true, name);
            return e;
        }
        catch (Exception e)
        {
            Fail(name + " -- threw " + e.GetType().Name + ": " + e.Message);
            return null;
        }
        Fail(name + " -- did not throw");
        return null;
    }

    public void Fail(string name)
    {
        ++Failed;
        Console.WriteLine("  FAIL  " + name);
    }

    public string FixtureServerPath => FixtureServer ?? throw new AssertionFailed("--fixture-server is required");
}
