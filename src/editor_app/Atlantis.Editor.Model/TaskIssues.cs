using System;
using System.Collections.Generic;
using System.Linq;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Assets;
using Atlantis.Gameplay.Authoring;

namespace Atlantis.Editor.Model;

public enum TaskResult { Requesting, Running, Paused, Refused, NotSent, Failed, Succeeded, Superseded, Unknown }

public sealed record TaskIssue(long Sequence, string Operation, string Target, AssetGuid? Asset, long? DocumentLifetime)
{
    internal int? ApplyAttempt { get; init; }
    public object? Receipt { get; init; }
    public ulong? Revision { get; init; }
    public TaskResult Result { get; init; }
    public GenerationId? Published { get; init; }
    public bool EditCommitted { get; init; }
    public bool WorkspaceNeedsRepair { get; init; }
    public string? Diagnostic { get; init; }
    public string? Guidance { get; init; }
    public bool Outstanding => Result is TaskResult.Requesting or TaskResult.Running or TaskResult.Paused;

    public string Summary(bool activationKnown, GenerationId? active) => Result switch
    {
        TaskResult.Succeeded when Published is GenerationId published => !activationKnown ? "Published · activation unconfirmed"
            : active == published ? "Published · active" : "Published · restart required",
        TaskResult.Failed when DocumentLifetime is not null => EditCommitted ? "Edit committed · Apply failed" : "Apply failed",
        TaskResult.Refused when DocumentLifetime is not null => EditCommitted ? "Edit committed · Apply failed" : "Apply refused",
        TaskResult.Succeeded when DocumentLifetime is not null => "Applied",
        TaskResult.Paused => "Pending · Runtime paused",
        TaskResult.NotSent => "Not sent",
        TaskResult.Unknown => "Outcome unknown",
        _ => Result.ToString(),
    };

    public string NextStep(bool activationKnown, GenerationId? active) =>
        Result == TaskResult.Unknown ? (DocumentLifetime is null
            ? "Check the target, job and workspace generations. Confirm settlement before retrying."
            : "Inspect the current document and Runtime projection. Confirm settlement before explicitly applying.")
        : Result == TaskResult.Succeeded && Published is GenerationId published && (!activationKnown || published != active)
            ? $"Check the active generation, then explicitly restart with --asset-generation {published}."
        : WorkspaceNeedsRepair ? "Inspect and repair the workspace before attempting a write."
        : Guidance ?? "Inspect the target and its existing status controls.";

    public string Details(bool activationKnown, GenerationId? active, bool includeGuidance = true)
    {
        var lines = new List<string> { Operation, "Target: " + Target, "Result: " + Summary(activationKnown, active) };
        if (Asset is AssetGuid guid) lines.Add("Asset GUID: " + guid);
        if (Receipt is not null) lines.Add("Receipt: " + Receipt);
        if (Revision is ulong revision)
            lines.Add((Receipt is ApplyId ? "Document revision: " : "Observed document revision: ") + revision);
        if (EditCommitted && Result != TaskResult.Failed && Result != TaskResult.Refused) lines.Add("Edit committed.");
        if (Published is GenerationId published)
        {
            lines.Add("Published generation: " + published);
            lines.Add("Active generation: " + (activationKnown ? active?.ToString() ?? "unnamed" : "unconfirmed"));
        }
        if (!string.IsNullOrEmpty(Diagnostic)) lines.Add(Diagnostic);
        if (WorkspaceNeedsRepair) lines.Add("Workspace needs repair.");
        if (includeGuidance) lines.Add(NextStep(activationKnown, active));
        return string.Join(Environment.NewLine, lines);
    }
}

/// <summary>A typed job is supplied only by its owner, with target identity checked there.</summary>
public sealed record AssetTaskReceipt(object Id, RebuildState State, AssetGuid? Asset, GenerationId? Published,
                                      string? Diagnostic, string? Guidance)
{
    public static AssetTaskReceipt From(RebuildJob job) => new(job.Id, job.State, job.Asset, job.Generation,
        job.Failure is { } f ? Failure(f.Stage, f.Code, f.Diagnostic, f.ExitCode) : null,
        job.Failure is null ? null : "Check the Assets job status and workspace before repeating the operation.");

    public static AssetTaskReceipt From(ImportJob job) =>
        Import(job.Id, job.State, job.RootGuid, job.Generation, job.Failure);

    public static AssetTaskReceipt From(TextureImportJob job) =>
        Import(job.Id, job.State, job.RootGuid, job.Generation, job.Failure);

    public static AssetTaskReceipt From(MaterialJob job) =>
        new(job.Id, job.State, job.SavedMaterial ?? job.Failure?.SavedMaterial, job.Generation,
            job.Failure is { } f ? Failure(f.Stage, f.Code, f.Diagnostic, f.ExitCode, "Source: " + f.SourceState) : null,
            Recovery(job.Failure?.SourceState));

    private static AssetTaskReceipt Import(object id, RebuildState state, AssetGuid? asset, GenerationId? published, ImportFailure? failure) =>
        new(id, state, asset ?? failure?.RegisteredRootGuid, published,
            failure is { } f ? Failure(f.Stage, f.Code, f.Diagnostic, f.ExitCode, "Registration: " + f.Registration) : null,
            Recovery(failure?.Registration));

    private static string? Recovery(RegistrationState? state) => state switch
    {
        RegistrationState.Committed => "After correcting the cause, use the existing Resume publication control when available.",
        RegistrationState.Unknown => "Inspect the workspace and check settlement before attempting another write.",
        RegistrationState.NotCommitted => "Correct the cause before starting a new operation.",
        _ => null,
    };

    private static string Failure<TStage, TCode>(TStage stage, TCode code, string diagnostic, uint? exitCode, string? settlement = null) =>
        $"{stage}/{code}: {diagnostic}" + (exitCode is uint exit ? $" · exit {exit}" : "") +
        (settlement is null ? "" : " · " + settlement);
}

/// <summary>One local Apply attempt. An accepted receipt alone is independent of edit commit proof.</summary>
public sealed record ApplyTaskUpdate(int Attempt, TaskResult Result, bool EditCommitted, ApplyReceipt? Receipt,
                                     ulong? Revision, string? Diagnostic, bool Started = false, ReloadError? Error = null);

/// <summary>UI-thread projection. No work is scheduled from observation or display.</summary>
public sealed class TaskIssues
{
    private readonly List<TaskIssue> _rows = new();
    private long _sequence;
    public IReadOnlyList<TaskIssue> Rows => _rows;
    public bool Connected { get; private set; }
    public bool ActivationKnown { get; private set; }
    public GenerationId? ActiveGeneration { get; private set; }
    public event Action? Changed;

    public void Reset()
    {
        _rows.Clear();
        Connected = true;
        ActivationKnown = false;
        ActiveGeneration = null;
        Changed?.Invoke();
    }

    public void ObserveActivation(GenerationId? active)
    {
        if (!Connected || (ActivationKnown && ActiveGeneration == active)) return;
        ActiveGeneration = active;
        ActivationKnown = true;
        Changed?.Invoke();
    }

    public long Begin(string operation, string target, AssetGuid? asset = null, long? document = null, int? applyAttempt = null)
    {
        var row = new TaskIssue(++_sequence, operation, target, asset, document) { ApplyAttempt = applyAttempt };
        _rows.Insert(0, row);
        Changed?.Invoke();
        return row.Sequence;
    }

    public void Admit(long attempt, AssetTaskReceipt? receipt, string? diagnostic = null, TaskResult refusal = TaskResult.Refused)
    {
        if (Find(attempt) is not { Result: TaskResult.Requesting, DocumentLifetime: null } row) return;
        Update(receipt is null ? row with { Result = refusal, Diagnostic = diagnostic } : FromReceipt(row, receipt));
    }

    public void Observe(long attempt, object id, AssetTaskReceipt? receipt, string? diagnostic = null)
    {
        if (Find(attempt) is not { DocumentLifetime: null } row || !Equals(row.Receipt, id)) return;
        if (receipt is not null && Equals(receipt.Id, id))
        {
            if (row.Outstanding || row.Result == TaskResult.Unknown) Update(FromReceipt(row, receipt));
        }
        else if (row.Outstanding)
            Update(row with { Result = TaskResult.Unknown, Diagnostic = diagnostic ?? "The matching job receipt is unavailable." });
    }

    public void NeedsRepair(long attempt)
    {
        if (Find(attempt) is { } row) Update(row with { WorkspaceNeedsRepair = true });
    }

    private static TaskIssue FromReceipt(TaskIssue row, AssetTaskReceipt receipt) => row with
    {
        Receipt = receipt.Id,
        Asset = receipt.Asset ?? row.Asset,
        Published = receipt.Published,
        Result = receipt.State switch
        {
            RebuildState.Succeeded => TaskResult.Succeeded,
            RebuildState.Failed => TaskResult.Failed,
            _ => TaskResult.Running,
        },
        Diagnostic = receipt.Diagnostic,
        Guidance = receipt.Guidance,
    };

    public void ObserveApply(long lifetime, string target, AssetGuid? scene, ApplyTaskUpdate update)
    {
        var row = _rows.FirstOrDefault(task => task.DocumentLifetime == lifetime && task.ApplyAttempt == update.Attempt);
        if (row is null && update.Started && Connected)
            row = Find(Begin("Scene Apply", target, scene, lifetime, update.Attempt));
        if (row is null || !row.Outstanding ||
            (row.Receipt is ApplyId owner && update.Receipt is { } reported && owner != reported.Id)) return;
        string? guidance = update.Result switch
        {
            TaskResult.Failed when update.Error == ReloadError.RuntimeStopped =>
                "The Runtime stopped. The document and its history settle independently. Inspect before restarting.",
            TaskResult.Failed or TaskResult.Refused => "Correct the cause, then use the existing Apply control. Document history is retained.",
            TaskResult.Superseded => "Inspect the current document and projection before using the existing Apply control.",
            TaskResult.Paused => "Use Step or Resume to let the accepted Apply settle.",
            _ => null,
        };
        Update(row with { Result = update.Result, Receipt = update.Receipt?.Id ?? row.Receipt,
            Revision = update.Receipt?.DocumentRevision ?? update.Revision, EditCommitted = update.EditCommitted,
            Diagnostic = update.Diagnostic, Guidance = guidance });
    }

    public TaskIssue? Find(long sequence) => _rows.FirstOrDefault(row => row.Sequence == sequence);

    public void RetireDocuments(long current)
    {
        foreach (var row in _rows.Where(row => row.DocumentLifetime is long lifetime && lifetime != current && row.Outstanding).ToArray())
            Update(row with { Result = TaskResult.Unknown, Diagnostic = "The document was replaced before settlement could be observed." });
        Changed?.Invoke();
    }

    public void Disconnect()
    {
        Connected = false;
        ActivationKnown = false;
        ActiveGeneration = null;
        foreach (var row in _rows.Where(row => row.Outstanding).ToArray())
            Update(row with { Result = TaskResult.Unknown, Diagnostic = "The session disconnected before settlement could be observed." });
        Changed?.Invoke();
    }

    private void Update(TaskIssue row)
    {
        int index = _rows.FindIndex(current => current.Sequence == row.Sequence);
        if (index < 0 || _rows[index] == row) return;
        _rows[index] = row;
        int settled = 0;
        for (int i = 0; i < _rows.Count; ++i)
        {
            if (_rows[i].Outstanding || ++settled <= 32) continue;
            _rows.RemoveAt(i--);
        }
        Changed?.Invoke();
    }
}
