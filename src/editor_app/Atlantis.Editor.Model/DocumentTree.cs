// Plan 0068 M2 (Spec 0068 R3, Fact 1, Fact 8; Q6): the Hierarchy's model --
// the authoring document's nodes as a tree under their parents, in the
// document's order, each labelled by its GUID's first eight hex digits and
// its components' kinds, prefixed by its name when present (Spec 0069).
// Built from ListNodes()/ListComponents() and kept current by every
// client's authoring events (no polling). A new C# model with no C++
// reference: ADR-0137 D2, tested against a fake document. UI-free.
using System;
using System.Collections.Generic;
using System.Linq;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Authoring;

namespace Atlantis.Editor.Model;

public sealed class DocumentTree
{
    private sealed class Row
    {
        public EntityGuid? Parent;
        public object ParentStamp = new();
        public string Name = "";
        public AssetGuid? PrefabOrigin;
        public List<TypeId> Components = new();
        public readonly Dictionary<TypeId, ComponentState> Lifetimes = new();
    }

    private sealed class ComponentState
    {
        public readonly Dictionary<FieldId, object> Changes = new();
    }

    private readonly Dictionary<EntityGuid, Row> _rows = new();
    private readonly List<EntityGuid> _order = new();  // the document's order
    // The children of every parent (and the roots), in the document's order; rebuilt on first use after a change.
    private Dictionary<EntityGuid, List<EntityGuid>>? _children;
    private List<EntityGuid>? _roots;

    /// <summary>Increments with every change, for a view to know when to refresh.</summary>
    public ulong Generation { get; private set; }

    public ulong NodeLifetimeGeneration { get; private set; }

    public int Count => _order.Count;

    public IReadOnlyList<EntityGuid> Order => _order;
    public bool OrderSynchronized { get; private set; } = true;

    public bool Contains(EntityGuid node) => _rows.ContainsKey(node);
    /// <summary>An opaque identity for this occurrence of a node in the tree.</summary>
    public object? IncarnationOf(EntityGuid node) => _rows.GetValueOrDefault(node);

    private ComponentState? ComponentAt(SceneAddress address) =>
        _rows.GetValueOrDefault(address.Node)?.Lifetimes.GetValueOrDefault(address.Component);

    public object? ComponentIncarnationOf(SceneAddress address) => ComponentAt(address);

    public object? PropertyStamp(SceneAddress address) =>
        ComponentAt(address) is { } component ? component.Changes.GetValueOrDefault(address.Field) ?? component : null;

    public EntityGuid? ParentOf(EntityGuid node) => _rows.TryGetValue(node, out Row? row) ? row.Parent : null;
    public object? ParentStamp(EntityGuid node) => _rows.GetValueOrDefault(node)?.ParentStamp;

    public string NameOf(EntityGuid node) => _rows.TryGetValue(node, out Row? row) ? row.Name : "";
    public AssetGuid? PrefabOriginOf(EntityGuid node) => _rows.GetValueOrDefault(node)?.PrefabOrigin;

    public IReadOnlyList<TypeId> ComponentsOf(EntityGuid node) =>
        _rows.TryGetValue(node, out Row? row) ? row.Components : Array.Empty<TypeId>();

    /// <summary>The children of <paramref name="parent"/> (the roots for null), in the document's order.</summary>
    public IReadOnlyList<EntityGuid> ChildrenOf(EntityGuid? parent)
    {
        if (_children is null || _roots is null)
        {
            _children = new Dictionary<EntityGuid, List<EntityGuid>>();
            _roots = new List<EntityGuid>();
            foreach (EntityGuid node in _order)
            {
                if (_rows[node].Parent is EntityGuid p)
                {
                    if (!_children.TryGetValue(p, out List<EntityGuid>? list)) _children[p] = list = new List<EntityGuid>();
                    list.Add(node);
                }
                else
                {
                    _roots.Add(node);
                }
            }
        }
        if (parent is not EntityGuid key) return _roots;
        return _children.TryGetValue(key, out List<EntityGuid>? children) ? children : Array.Empty<EntityGuid>();
    }

    public EntityGuid? AdjacentSibling(EntityGuid node, SiblingDirection direction)
    {
        IReadOnlyList<EntityGuid> siblings = ChildrenOf(ParentOf(node));
        for (int i = 0; i < siblings.Count; ++i)
        {
            if (siblings[i] != node) continue;
            int adjacent = i + (direction == SiblingDirection.Up ? -1 : 1);
            return adjacent >= 0 && adjacent < siblings.Count ? siblings[adjacent] : null;
        }
        return null;
    }

    public int PositionOf(EntityGuid node) => _order.IndexOf(node);

    public IReadOnlyList<EntityGuid> SiblingInterval(EntityGuid? parent, EntityGuid first, EntityGuid last)
    {
        var siblings = ChildrenOf(parent).ToList();
        int begin = siblings.IndexOf(first), end = siblings.IndexOf(last);
        return begin >= 0 && end >= begin ? siblings.GetRange(begin, end - begin + 1) : Array.Empty<EntityGuid>();
    }

    public bool MatchesOrderDelta(NodeOrderChanged change) =>
        OrderSynchronized && change.Node != change.Sibling && change.Position < (ulong)_order.Count &&
        change.SiblingPosition < (ulong)_order.Count &&
        _order[(int)change.Position] == change.Sibling && _order[(int)change.SiblingPosition] == change.Node;

    public bool MatchesOrderDelta(NodeMovedInSiblingOrder change) =>
        OrderSynchronized && change.FromPosition != change.Position &&
        change.FromPosition < (ulong)_order.Count && change.Position < (ulong)_order.Count &&
        _order[(int)change.FromPosition] == change.Node && ParentOf(change.Node) == change.Parent &&
        ParentOf(_order[(int)change.Position]) == change.Parent;

    private void RotateSiblingSlots(NodeMovedInSiblingOrder change)
    {
        int from = (int)change.FromPosition, to = (int)change.Position;
        int step = from < to ? 1 : -1;
        for (int slot = from + step; slot != to + step; slot += step)
        {
            if (ParentOf(_order[slot]) != change.Parent) continue;
            (_order[from], _order[slot]) = (_order[slot], _order[from]);
            from = slot;
        }
    }

    /// <summary>Accepts an ordered snapshot after the caller fences its revision and drains covered lifecycle events.</summary>
    public bool ReconcileOrder(IReadOnlyList<NodeSummary> nodes)
    {
        if (nodes.Count != _rows.Count || nodes.Select(n => n.Node).Distinct().Count() != nodes.Count ||
            nodes.Any(n => !_rows.TryGetValue(n.Node, out Row? row) || row.Parent != n.Parent || row.Name != n.Name ||
                row.PrefabOrigin != n.PrefabOrigin))
            return false;
        _order.Clear();
        _order.AddRange(nodes.Select(n => n.Node));
        OrderSynchronized = true;
        Changed();
        return true;
    }

    /// <summary>The node's descendants, depth first, each child before its own children, in the document's order.</summary>
    public IReadOnlyList<EntityGuid> DescendantsOf(EntityGuid node)
    {
        var result = new List<EntityGuid>();
        var stack = new Stack<EntityGuid>(ChildrenOf(node).Reverse());
        while (stack.Count > 0)
        {
            EntityGuid next = stack.Pop();
            result.Add(next);
            foreach (EntityGuid child in ChildrenOf(next).Reverse()) stack.Push(child);
        }
        return result;
    }

    /// <summary>Whether <paramref name="ancestor"/> is <paramref name="node"/> or one of its ancestors.</summary>
    public bool IsSelfOrAncestor(EntityGuid ancestor, EntityGuid node)
    {
        EntityGuid? current = node;
        var seen = new HashSet<EntityGuid>();
        while (current is EntityGuid c && seen.Add(c))
        {
            if (c == ancestor) return true;
            current = ParentOf(c);
        }
        return false;
    }

    /// <summary>The name when present, then GUID prefix and component kinds; GUIDs still identify rows.</summary>
    public string Label(EntityGuid node, IReadOnlyList<SchemaType> schema)
    {
        string prefix = node.Value.ToString("N")[..8];
        IEnumerable<string> kinds = ComponentsOf(node).Select(c => SceneSchema.NameOf(schema, c))
                                                      .OrderBy(k => k == "Transform" ? 0 : 1).ThenBy(k => k, StringComparer.Ordinal);
        string name = NameOf(node);
        return (name.Length > 0 ? name + " · " : "") + prefix + " · " + string.Join(" ", kinds);
    }

    /// <summary>Re-reads the whole document (on open and on DocumentReopened).</summary>
    public void Rebuild(IAuthoringDocument document)
    {
        ++NodeLifetimeGeneration;
        _rows.Clear();
        _order.Clear();
        IReadOnlyList<NodeSummary> nodes = document.ListNodes();
        IReadOnlyList<IReadOnlyList<TypeId>> components = document.ListComponents(nodes.Select(n => n.Node).ToList());
        for (int i = 0; i < nodes.Count; ++i)
        {
            _order.Add(nodes[i].Node);
            var row = new Row { Parent = nodes[i].Parent, Name = nodes[i].Name,
                PrefabOrigin = nodes[i].PrefabOrigin, Components = components[i].ToList() };
            foreach (TypeId type in row.Components) row.Lifetimes.TryAdd(type, new ComponentState());
            _rows[nodes[i].Node] = row;
        }
        OrderSynchronized = true;
        Changed();
    }

    private void Changed()
    {
        _children = null;
        _roots = null;
        ++Generation;
    }

    /// <summary>Follows one authoring event; returns whether the tree changed.</summary>
    public bool Apply(Event e, IAuthoringDocument document)
    {
        switch (e)
        {
            case NodeMovedInSiblingOrder moved:
                if (MatchesOrderDelta(moved)) RotateSiblingSlots(moved);
                else
                {
                    if (!OrderSynchronized) return false;
                    OrderSynchronized = false;
                }
                break;
            case NodeOrderChanged exchanged:
                if (!MatchesOrderDelta(exchanged))
                {
                    if (!OrderSynchronized) return false;
                    OrderSynchronized = false;
                    break;
                }
                _order[(int)exchanged.Position] = exchanged.Node;
                _order[(int)exchanged.SiblingPosition] = exchanged.Sibling;
                break;
            case NodeCreated created:
            {
                int position = (int)Math.Min(created.Position, (ulong)_order.Count);
                ++NodeLifetimeGeneration;
                _order.Insert(position, created.Node);
                // A node is created with its Transform (a required component, no ComponentAdded for it); the rest arrive as events.
                var fresh = new Row { PrefabOrigin = created.PrefabOrigin };
                if (SceneSchema.TypeOf(document.Schema, "Transform") is TypeId transform)
                {
                    fresh.Components.Add(transform);
                    fresh.Lifetimes.Add(transform, new ComponentState());
                }
                _rows[created.Node] = fresh;
                break;
            }
            case NodeDestroyed destroyed:
                if (!_rows.Remove(destroyed.Node)) return false;
                ++NodeLifetimeGeneration;
                _order.Remove(destroyed.Node);
                break;
            case ParentChanged moved:
                if (!_rows.TryGetValue(moved.Node, out Row? row)) return false;
                row.Parent = moved.Parent;
                row.ParentStamp = new object();
                break;
            case NodeNameChanged renamed:
                if (!_rows.TryGetValue(renamed.Node, out Row? named)) return false;
                named.Name = renamed.Name;
                break;
            case ComponentAdded added:
                if (!_rows.TryGetValue(added.Node, out Row? withAdded) || withAdded.Components.Contains(added.Component)) return false;
                withAdded.Components.Add(added.Component);
                withAdded.Lifetimes.Add(added.Component, new ComponentState());
                withAdded.Components.Sort((a, b) => a.Value.CompareTo(b.Value));
                break;
            case ComponentRemoved removed:
                if (!_rows.TryGetValue(removed.Node, out Row? withRemoved) || !withRemoved.Components.Remove(removed.Component)) return false;
                withRemoved.Lifetimes.Remove(removed.Component);
                break;
            case Atlantis.Gameplay.Authoring.PropertyChanged change:
                if (ComponentAt(change.Address) is { } component)
                    component.Changes[change.Address.Field] = new object();
                return false;
            case DocumentReopened:
                Rebuild(document);
                return true;
            default:
                return false;
        }
        Changed();
        return true;
    }
}

/// <summary>The Editor's ordered local selection; single-target operations use Node.</summary>
public sealed class Selection
{
    public IReadOnlyList<EntityGuid> Nodes { get; private set; } = Array.Empty<EntityGuid>();
    public EntityGuid? Node => Nodes.Count == 1 ? Nodes[0] : null;
    public ulong Generation { get; private set; }

    public void Select(EntityGuid? node) => Set(node is EntityGuid guid ? new[] { guid } : Array.Empty<EntityGuid>());

    public bool Set(IEnumerable<EntityGuid> nodes)
    {
        EntityGuid[] next = nodes.Distinct().Take(RequireDocumentRevision.MaxWrites + 1).ToArray();
        if (next.Length > RequireDocumentRevision.MaxWrites) return false;
        if (!Nodes.SequenceEqual(next))
        {
            Nodes = next;
            ++Generation;
        }
        return true;
    }

    public bool Toggle(EntityGuid node) =>
        Nodes.Contains(node) ? Set(Nodes.Where(item => item != node)) : Set(Nodes.Append(node));

    public void Apply(Event e, DocumentTree tree)
    {
        if (e is NodeDestroyed destroyed) Set(Nodes.Where(n => n != destroyed.Node));
        if (e is DocumentReopened)
            Set(Nodes.Count > 1 ? Array.Empty<EntityGuid>() : Nodes.Where(tree.Contains));
    }
}

public enum ApplyStatus { Idle, Applying, Applied, PendingPaused, Failed, Superseded, Unknown, NeedsApply }

/// <summary>Caller-thread apply observation (0083). Document commits settle independently;
/// only this receipt's typed result settles projection. Recovery never resubmits itself.</summary>
public sealed class ApplyController
{
    private readonly IAuthoringDocument _document;
    private bool _inFlight, _again, _paused, _suspended, _recovering, _stopped, _needGlobalStatus;
    private ApplyReceipt? _receipt;
    private ApplySnapshot? _snapshot;
    private ulong _localRecoveryCommits;
    private string _notice = "";
    private int _taskAttempt;
    private bool _taskInProgress, _taskCommitProof, _coalescedCommitProof;
    public event Action<ApplyTaskUpdate>? TaskObserved;

    private void BeginTask(bool committed)
    {
        ++_taskAttempt;
        _taskInProgress = true;
        _taskCommitProof = committed;
        ObserveTask(TaskResult.Requesting, started: true);
    }

    private void ObserveTask(TaskResult result, string? diagnostic = null, bool started = false, ReloadError? error = null, bool withoutReceipt = false)
    {
        if (!_taskInProgress) return;
        if (result is not (TaskResult.Requesting or TaskResult.Running or TaskResult.Paused)) _taskInProgress = false;
        var receipt = started || withoutReceipt ? null : _receipt;
        TaskObserved?.Invoke(new(_taskAttempt, result, _taskCommitProof, receipt,
            receipt?.DocumentRevision ?? _snapshot?.CurrentDocumentRevision, diagnostic, started, error));
    }

    public ApplyController(IAuthoringDocument document) => _document = document;
    public ApplyStatus Status { get; private set; } = ApplyStatus.Idle;
    public ulong Generation { get; private set; }
    public ApplyError? LastError { get; private set; }
    public ReloadError? LastReloadError { get; private set; }
    public int Issued { get; private set; }
    public ApplyReceipt? Receipt => _receipt;
    public bool Suspended => _suspended;
    public bool CanPreview => !_recovering && !_suspended && !_stopped;
    public bool CanApply => !_inFlight && !_stopped;
    public ApplySnapshot? Snapshot => _snapshot;
    public string Text
    {
        get
        {
            string text = Status switch
            {
                ApplyStatus.Applying => "applying",
                ApplyStatus.PendingPaused => "pending: paused (applies on Step or Resume)",
                ApplyStatus.Applied => "applied · generation " + Generation,
                ApplyStatus.Failed => "failed: " + (LastError?.ToString() ?? LastReloadError?.ToString() ?? _notice) + "; document/history kept; edit then Retry",
                ApplyStatus.Superseded => "superseded: " + _notice + "; document/history kept; explicit Retry",
                ApplyStatus.Unknown => "unknown completion: " + _notice + "; inspect before explicitly applying; an older request may still take effect",
                ApplyStatus.NeedsApply => _notice + "; apply current document",
                _ => "",
            };
            if (_receipt is ApplyReceipt r) text += " · apply " + r.Id + " · revision " + r.DocumentRevision;
            if (_snapshot is ApplySnapshot view && Status != ApplyStatus.Idle)
                text += " · document revision " + (view.CurrentDocumentRevision?.ToString() ?? "none") +
                    (view.Projection is ApplyProjection p ? " · Runtime origin generation " + p.Generation + "/revision " + p.Receipt.DocumentRevision : " · Runtime startup projection");
            return text;
        }
    }
    // Readiness is observation, never ownership of another client's latest receipt.
    public void Initialize(ApplySnapshot snapshot)
    {
        _snapshot = snapshot;
        if (snapshot.Request is { State: ApplyState.Failed } failed)
        { LastReloadError = failed.Error; _stopped = failed.Error == ReloadError.RuntimeStopped; Suspend(ApplyStatus.Failed, "observed apply " + failed.Receipt.Id); }
        else if (snapshot.Request is { State: ApplyState.Superseded } replaced)
            Suspend(ApplyStatus.Superseded, "observed apply " + replaced.Receipt.Id);
    }
    /// <summary>An explicit request; normal local requests while in flight coalesce.</summary>
    public void Request()
    {
        if (_stopped) return;
        if (_inFlight)
        {
            if (!_recovering && !_suspended) _again = true;
            return;
        }
        _recovering |= _suspended;
        Issue(committed: false);
    }
    /// <summary>User recovery: duplicate clicks cannot queue an additional apply.</summary>
    public void Retry() { if (CanApply) Request(); }
    public IReadOnlyList<CommandFailure> Commit(IReadOnlyList<Command> commands) => Commit(commands, null)!;

    public IReadOnlyList<CommandFailure>? Commit(IReadOnlyList<Command> commands,
        Func<bool, CommitOutcome?>? guardedCommit)
    {
        bool deferApply = _suspended || _inFlight;
        CommitOutcome outcome;
        try
        {
            if (guardedCommit is not null)
            {
                if (guardedCommit(!deferApply) is not { } guarded) return null;
                outcome = guarded;
            }
            else if (deferApply)
            {
                _document.SubmitTransaction(commands);
                outcome = new CommitOutcome(_document.DrainFailures(), false, null);
            }
            else outcome = _document.CommitAndApply(commands);
        }
        catch (RemoteException error) when (!deferApply)
        {
            // A pipelined commit can fail before its Apply acceptance is decoded.
            // The transport provides no proof of the commit or Apply in this case.
            BeginTask(committed: false);
            ObserveTask(TaskResult.Unknown, "Commit or Apply acceptance unavailable: " + error.Error, withoutReceipt: true);
            throw;
        }
        if (deferApply && outcome.Failures.Count == 0) AfterLocalEdit(commands.Count != 0);
        if (outcome.ApplySent)
        {
            ++Issued;
            _receipt = null;
            BeginTask(commands.Count != 0 && outcome.Failures.Count == 0);
            if (outcome.ApplyError is ApplyError error)
            {
                LastError = error;
                ObserveTask(TaskResult.Refused, "Apply refusal: " + error);
                Suspend(ApplyStatus.Failed, error.ToString());
            }
            else if (outcome.Receipt is ApplyReceipt receipt) Accepted(receipt);
            else
            {
                ObserveTask(TaskResult.Unknown, "Acceptance reply had no receipt.");
                Suspend(ApplyStatus.Unknown, "acceptance reply had no receipt");
            }
        }
        return outcome.Failures;
    }
    public void AfterLocalEdit(bool hasCommands)
    {
        _needGlobalStatus = true;
        if (_suspended || _stopped) return;
        if (_inFlight)
        {
            if (!_recovering || hasCommands) _again = true;
            _coalescedCommitProof |= hasCommands;
            if (_recovering && hasCommands) ++_localRecoveryCommits;
            return;
        }
        Issue(committed: hasCommands);
    }

    public void LostApplyAcceptance()
    {
        ObserveTask(TaskResult.Unknown, "Apply reply lost after document commit.");
        Suspend(ApplyStatus.Unknown, "apply reply lost after document commit");
    }

    public void RequireExplicitApplyAfterUnverifiedSwap()
    {
        if (!_suspended) RequireExplicitApply("sibling swap could not be verified");
    }

    public void RequireExplicitApply(string reason)
    {
        if (_stopped) return;
        if (_suspended && !_inFlight) return;
        _needGlobalStatus = true;
        if (!_inFlight)
        {
            Suspend(ApplyStatus.NeedsApply, reason);
            return;
        }
        // Keep polling the accepted snapshot, but drop any follow-up based on the uncertain revision.
        _again = false;
        _coalescedCommitProof = false;
        _suspended = true;
        _notice = reason;
    }
    public void SetPaused(bool paused)
    {
        _paused = paused;
        if (_inFlight)
        {
            Status = paused ? ApplyStatus.PendingPaused : ApplyStatus.Applying;
            ObserveTask(paused ? TaskResult.Paused : TaskResult.Running);
        }
    }
    public void Apply(Event e)
    {
        if (e is DocumentReopened)
        {
            ObserveTask(TaskResult.Unknown, "The document was replaced before the Apply settled.");
            _receipt = null; _needGlobalStatus = true;
            Suspend(ApplyStatus.NeedsApply, "document reopened; a previously accepted snapshot may still swap");
        }
        // Events are hints only. The caller processes the whole batch (including
        // any later Reopen) before Poll can settle/issue a coalesced request.
        else _needGlobalStatus = true;
    }
    /// <summary>Run even when drainEvents is empty. Owned synchronous result; no UI-thread wait.</summary>
    public void Poll()
    {
        if (!_inFlight || _receipt is not ApplyReceipt expected)
        {
            if (_needGlobalStatus) { var global = _document.ApplyStatus(); if (global.IsOk) _snapshot = global.Value; _needGlobalStatus = false; }
            return;
        }
        var queried = _document.ApplyStatus(expected.Id);
        if (!queried.IsOk)
        {
            ObserveTask(TaskResult.Unknown, "UnknownApply " + expected.Id);
            Suspend(ApplyStatus.Unknown, "UnknownApply " + expected.Id);
            return;
        }
        ApplySnapshot view = queried.Value!; _snapshot = view; _needGlobalStatus = false;
        if (view.Request is not ApplyRecord record || record.Receipt != expected)
        {
            ObserveTask(TaskResult.Unknown, "The returned Apply receipt did not match.");
            Suspend(ApplyStatus.Unknown, "mismatched receipt");
            return;
        }
        if (record.State == ApplyState.Pending) return;
        _inFlight = false;
        if (record.State == ApplyState.Failed)
        {
            LastReloadError = record.Error; _stopped = record.Error == ReloadError.RuntimeStopped;
            string reason = record.EnvironmentDetail is { } environment
                ? $"Scene environment {environment.Requested.Asset?.ToString() ?? "Inherit"}. " +
                    (environment.Reason == SceneEnvironmentFailure.BuildOpenRequired
                        ? "Save, then use Saved Scenes Build/Open to activate this choice."
                        : $"{environment.Reason}. Check the cooked environment before applying again.")
                : record.Error.ToString()!;
            ObserveTask(TaskResult.Failed,
                $"Runtime reload: {reason} " + (record.Error == ReloadError.RuntimeStopped
                    ? "The Runtime stopped." : "The previous Runtime projection remains active."), error: record.Error);
            Suspend(ApplyStatus.Failed, reason); return;
        }
        if (record.State == ApplyState.Superseded)
        {
            ObserveTask(TaskResult.Superseded, "Superseded by Apply " + record.SupersededBy);
            Suspend(ApplyStatus.Superseded, "by " + record.SupersededBy);
            return;
        }
        Generation = record.Generation!.Value; Status = ApplyStatus.Applied;
        ObserveTask(TaskResult.Succeeded, "Runtime projection generation: " + Generation);
        if (_suspended)
        {
            Suspend(ApplyStatus.NeedsApply, _notice);
            return;
        }
        if (_recovering &&
            (view.Projection is not ApplyProjection projection || projection.Receipt != expected ||
             projection.Generation != Generation ||
             view.CurrentDocumentRevision != expected.DocumentRevision + _localRecoveryCommits))
        {
            Suspend(ApplyStatus.NeedsApply, "document/projection changed");
            return;
        }
        if (_again)
        {
            bool committed = _coalescedCommitProof;
            _again = _coalescedCommitProof = false;
            Issue(committed);
            return;
        }
        if (_recovering)
        {
            _recovering = false;
            _suspended = false;
        }
    }
    private void Suspend(ApplyStatus status, string notice)
    {
        _inFlight = false; _again = _coalescedCommitProof = false; _suspended = true; _recovering = true;
        _localRecoveryCommits = 0;
        _notice = notice; Status = status;
    }
    private void Accepted(ApplyReceipt receipt)
    {
        _receipt = receipt; _inFlight = true; _suspended = false; _needGlobalStatus = false;
        _localRecoveryCommits = 0;
        _snapshot = new ApplySnapshot(receipt.DocumentRevision, new ApplyRecord(receipt, ApplyState.Pending), _snapshot?.Projection);
        LastError = null; LastReloadError = null; _notice = "";
        Status = _paused ? ApplyStatus.PendingPaused : ApplyStatus.Applying;
        ObserveTask(_paused ? TaskResult.Paused : TaskResult.Running);
    }
    private void Issue(bool committed)
    {
        ++Issued; _receipt = null;
        BeginTask(committed);
        try
        {
            var accepted = _document.ApplyTracked();
            if (!accepted.IsOk)
            {
                LastError = accepted.Error;
                ObserveTask(TaskResult.Refused, "Apply refusal: " + accepted.Error);
                Suspend(ApplyStatus.Failed, accepted.Error.ToString());
                return;
            }
            Accepted(accepted.Value!);
        }
        catch (RemoteException error)
        {
            ObserveTask(TaskResult.Unknown, "Apply acceptance unavailable: " + error.Error);
            throw;
        }
    }
}
