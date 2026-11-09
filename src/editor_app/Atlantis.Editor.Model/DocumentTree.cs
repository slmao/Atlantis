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
        public string Name = "";
        public List<TypeId> Components = new();
    }

    private readonly Dictionary<EntityGuid, Row> _rows = new();
    private readonly List<EntityGuid> _order = new();  // the document's order
    // The children of every parent (and the roots), in the document's order; rebuilt on first use after a change.
    private Dictionary<EntityGuid, List<EntityGuid>>? _children;
    private List<EntityGuid>? _roots;

    /// <summary>Increments with every change, for a view to know when to refresh.</summary>
    public ulong Generation { get; private set; }

    public int Count => _order.Count;

    public IReadOnlyList<EntityGuid> Order => _order;

    public bool Contains(EntityGuid node) => _rows.ContainsKey(node);

    public EntityGuid? ParentOf(EntityGuid node) => _rows.TryGetValue(node, out Row? row) ? row.Parent : null;

    public string NameOf(EntityGuid node) => _rows.TryGetValue(node, out Row? row) ? row.Name : "";

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
        _rows.Clear();
        _order.Clear();
        IReadOnlyList<NodeSummary> nodes = document.ListNodes();
        IReadOnlyList<IReadOnlyList<TypeId>> components = document.ListComponents(nodes.Select(n => n.Node).ToList());
        for (int i = 0; i < nodes.Count; ++i)
        {
            _order.Add(nodes[i].Node);
            _rows[nodes[i].Node] = new Row { Parent = nodes[i].Parent, Name = nodes[i].Name, Components = components[i].ToList() };
        }
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
            case NodeCreated created:
            {
                int position = (int)Math.Min(created.Position, (ulong)_order.Count);
                _order.Insert(position, created.Node);
                // A node is created with its Transform (a required component, no ComponentAdded for it); the rest arrive as events.
                var fresh = new Row();
                if (SceneSchema.TypeOf(document.Schema, "Transform") is TypeId transform) fresh.Components.Add(transform);
                _rows[created.Node] = fresh;
                break;
            }
            case NodeDestroyed destroyed:
                if (!_rows.Remove(destroyed.Node)) return false;
                _order.Remove(destroyed.Node);
                break;
            case ParentChanged moved:
                if (!_rows.TryGetValue(moved.Node, out Row? row)) return false;
                row.Parent = moved.Parent;
                break;
            case NodeNameChanged renamed:
                if (!_rows.TryGetValue(renamed.Node, out Row? named)) return false;
                named.Name = renamed.Name;
                break;
            case ComponentAdded added:
                if (!_rows.TryGetValue(added.Node, out Row? withAdded) || withAdded.Components.Contains(added.Component)) return false;
                withAdded.Components.Add(added.Component);
                withAdded.Components.Sort((a, b) => a.Value.CompareTo(b.Value));
                break;
            case ComponentRemoved removed:
                if (!_rows.TryGetValue(removed.Node, out Row? withRemoved) || !withRemoved.Components.Remove(removed.Component)) return false;
                break;
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

/// <summary>
/// The one selection the Viewport pick, the Hierarchy and the Inspector share (Spec 0068 Q6, S-a). A node any client
/// destroys is deselected; a selection persists across applies (GUIDs persist).
/// </summary>
public sealed class Selection
{
    public EntityGuid? Node { get; private set; }

    /// <summary>Increments with every change.</summary>
    public ulong Generation { get; private set; }

    public void Select(EntityGuid? node)
    {
        if (node == Node) return;
        Node = node;
        ++Generation;
    }

    /// <summary>Follows one authoring event: the selected node destroyed, or the document reopened without it.</summary>
    public void Apply(Event e, DocumentTree tree)
    {
        if (e is NodeDestroyed destroyed && destroyed.Node == Node) Select(null);
        if (e is DocumentReopened && Node is EntityGuid n && !tree.Contains(n)) Select(null);
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
            if (!_recovering) _again = true;
            return;
        }
        _recovering |= _suspended;
        Issue();
    }
    /// <summary>User recovery: duplicate clicks cannot queue an additional apply.</summary>
    public void Retry() { if (CanApply) Request(); }
    public IReadOnlyList<CommandFailure> Commit(IReadOnlyList<Command> commands)
    {
        if (_suspended || _inFlight)
        {
            _document.SubmitTransaction(commands);
            var failures = _document.DrainFailures();
            if (failures.Count == 0) AfterLocalEdit(commands.Count != 0);
            return failures;
        }
        CommitOutcome outcome = _document.CommitAndApply(commands);
        if (outcome.ApplySent)
        {
            ++Issued;
            if (outcome.ApplyError is ApplyError error) { _receipt = null; LastError = error; Suspend(ApplyStatus.Failed, error.ToString()); }
            else if (outcome.Receipt is ApplyReceipt receipt) Accepted(receipt);
            else Suspend(ApplyStatus.Unknown, "acceptance reply had no receipt");
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
            if (_recovering && hasCommands) ++_localRecoveryCommits;
            return;
        }
        Issue();
    }

    public void LostApplyAcceptance() => Suspend(ApplyStatus.Unknown, "apply reply lost after document commit");
    public void SetPaused(bool paused)
    {
        _paused = paused;
        if (_inFlight) Status = paused ? ApplyStatus.PendingPaused : ApplyStatus.Applying;
    }
    public void Apply(Event e)
    {
        if (e is DocumentReopened)
        {
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
        if (!queried.IsOk) { Suspend(ApplyStatus.Unknown, "UnknownApply " + expected.Id); return; }
        ApplySnapshot view = queried.Value!; _snapshot = view; _needGlobalStatus = false;
        if (view.Request is not ApplyRecord record || record.Receipt != expected)
        { Suspend(ApplyStatus.Unknown, "mismatched receipt"); return; }
        if (record.State == ApplyState.Pending) return;
        _inFlight = false;
        if (record.State == ApplyState.Failed)
        {
            LastReloadError = record.Error; _stopped = record.Error == ReloadError.RuntimeStopped;
            Suspend(ApplyStatus.Failed, record.Error.ToString()!); return;
        }
        if (record.State == ApplyState.Superseded)
        { Suspend(ApplyStatus.Superseded, "by " + record.SupersededBy); return; }
        Generation = record.Generation!.Value; Status = ApplyStatus.Applied;
        if (_recovering &&
            (view.Projection is not ApplyProjection projection || projection.Receipt != expected ||
             projection.Generation != Generation ||
             view.CurrentDocumentRevision != expected.DocumentRevision + _localRecoveryCommits))
        {
            Suspend(ApplyStatus.NeedsApply, "document/projection changed");
            return;
        }
        if (_again) { _again = false; Issue(); return; }
        if (_recovering)
        {
            _recovering = false;
            _suspended = false;
        }
    }
    private void Suspend(ApplyStatus status, string notice)
    {
        _inFlight = false; _again = false; _suspended = true; _recovering = true;
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
    }
    private void Issue()
    {
        ++Issued; _receipt = null;
        var accepted = _document.ApplyTracked();
        if (!accepted.IsOk) { LastError = accepted.Error; Suspend(ApplyStatus.Failed, accepted.Error.ToString()); return; }
        Accepted(accepted.Value!);
    }
}
