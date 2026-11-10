using System;
using System.Collections.Generic;

namespace Atlantis.Editor.Model;

public sealed record SavedSceneInput(string Name, string? ManagedRoot);
public sealed record SavedSceneFiles(string Source, string Catalog);
public sealed record SavedSceneBuild(SavedSceneInput Input, string Directory, string Executable,
                                     SavedSceneFiles Files, string ExecutableFingerprint);

/// <summary>Session-local admission and eligibility for Spec 0090. The shell owns filesystem and processes.</summary>
public sealed class SavedSceneAttempts
{
    private readonly HashSet<string> _children = new(StringComparer.Ordinal);
    public SavedSceneBuild? Result { get; private set; }
    public bool Building { get; private set; }
    public bool Starting { get; private set; }
    public bool Busy => Building || Starting;

    public bool CanBuild(SavedSceneInput? input) => input is not null && !Busy && !_children.Contains(input.Name);
    public bool CanOpen(SavedSceneInput? input) => CanBuild(input) && Result?.Input == input && Result is not null;

    public bool BeginBuild(SavedSceneInput input)
    {
        if (!CanBuild(input)) return false;
        Result = null;
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
    public void Invalidate() => Result = null;
    public bool HasChild(string name) => _children.Contains(name);
}
