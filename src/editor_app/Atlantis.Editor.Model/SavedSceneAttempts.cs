using System;
using System.Collections.Generic;

namespace Atlantis.Editor.Model;

public sealed record SavedSceneInput(string Name, string? ManagedRoot);
public sealed record SavedSceneFiles(string Source, string Catalog);
public sealed record SavedSceneBuild(SavedSceneInput Input, string Directory, string Executable,
                                     SavedSceneFiles Files, string ExecutableFingerprint, bool Recorded = false);

/// <summary>Session-local admission and eligibility. The shell owns filesystem and processes.</summary>
public sealed class SavedSceneAttempts
{
    private readonly HashSet<string> _children = new(StringComparer.Ordinal);
    private long _check;
    private long _recentCheck;
    public SavedSceneBuild? Result { get; private set; }
    public SavedSceneIdentity? RecentTarget { get; private set; }
    public bool RecentSelectionPending { get; private set; }
    public bool Building { get; private set; }
    public bool Starting { get; private set; }
    public bool Busy => Building || Starting;

    public bool CanBuild(SavedSceneInput? input) => input is not null && !Busy && !RecentSelectionPending && !_children.Contains(input.Name);
    public bool CanOpen(SavedSceneInput? input) => CanBuild(input) && Result?.Input == input && Result is not null;

    public bool BeginBuild(SavedSceneInput input)
    {
        if (!CanBuild(input)) return false;
        Invalidate();
        Building = true;
        return true;
    }

    public void FinishBuild(SavedSceneBuild? result)
    {
        Building = false;
        Result = result;
    }

    public SavedSceneBuild? BeginOpen(SavedSceneInput input)
    {
        if (!CanOpen(input)) return null;
        Starting = true;
        _children.Add(input.Name);
        return Result;
    }

    public void FinishOpen(string name, bool started)
    {
        Starting = false;
        if (!started) _children.Remove(name);
    }

    public void ChildExited(string name) => _children.Remove(name);
    public void Invalidate() { ++_check; Result = null; }
    public long BeginCheck() { Invalidate(); return _check; }
    public bool FinishCheck(long check, SavedSceneBuild? result)
    {
        if (check != _check || Busy) return false;
        Result = result;
        return true;
    }

    public long BeginRecentSelection()
    {
        RecentSelectionPending = true;
        return ++_recentCheck;
    }

    public void CancelRecentSelection()
    {
        ++_recentCheck;
        RecentSelectionPending = false;
    }

    public bool FinishRecentSelection(long check, SavedSceneIdentity? target)
    {
        if (check != _recentCheck || !RecentSelectionPending || Busy) return false;
        RecentSelectionPending = false;
        if (target is not null)
        {
            RecentTarget = target;
            Invalidate();
        }
        return true;
    }

    public void SelectOrdinary()
    {
        CancelRecentSelection();
        RecentTarget = null;
        Invalidate();
    }

    public bool HasChild(string name) => _children.Contains(name);
}
