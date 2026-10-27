// Plan 0068 M1 (Spec 0068 R1, R8; ADR-0124 D1-D4, ADR-0135 D4, ADR-0137 D1):
// E2's edit history (src/editor/include/atlantis/editor/model/
// edit_history.h, edit_target.h, edit_transaction.h), ported to C# -- this
// editor session's own edits, linear, at most MaxDepth deep; undo and redo
// write forward as new transactions; nothing is undoable until the
// transaction that applied it is settled; while any of the history's
// transactions is pending, undo and redo wait; before submitting, every
// address is read and compared with what the edit left there, a difference
// refusing the step and dropping the entry; an entry naming a destroyed node
// or a removed component is dropped. Addresses are the authoring document's
// (SceneAddress, Spec 0049 R7), values the SDK's PropertyValue. One
// difference of shape from C++: the target drains its own failures, so
// Update() takes none (the authoring target does, Spec 0065 R12). Held to the
// C++ history by tests/csharp/conformance/history_transcripts.jsonl. ADR-0136's
// structural records join in Plan 0068 M5. UI-free.
using System;
using System.Collections.Generic;
using System.Linq;
using Atlantis.Gameplay;
using Atlantis.Gameplay.Authoring;

namespace Atlantis.Editor.Model;

/// <summary>One write: an address, the value before the edit and the value after it.</summary>
public readonly record struct PropertyWrite(SceneAddress Address, PropertyValue Before, PropertyValue After);

/// <summary>
/// One logical edit: an ordered, non-empty list of writes under a label for the UI. Forward writes every After,
/// inverse every Before, in this order, each direction as one transaction.
/// </summary>
public sealed record EditTransaction(string Label, IReadOnlyList<PropertyWrite> Writes);

/// <summary>A submitted transaction, as its target numbers it.</summary>
public readonly record struct EditHandle(ulong First, ulong Count);

public enum Settlement
{
    Pending,
    Applied,
    Refused,
}

/// <summary>What the document reported gone: a whole node (no component), or one of its components.</summary>
public readonly record struct Invalidation(EntityGuid Node, TypeId? Component);

/// <summary>The one boundary through which the history reaches a document (ADR-0124 D4). Not thread-safe.</summary>
public interface IEditTarget
{
    /// <summary>The address's current value; null when it cannot be read (the node or component is gone).</summary>
    PropertyValue? Read(SceneAddress address);

    /// <summary>Submits the writes, in order, as one transaction and tracks its settlement.</summary>
    EditHandle Submit(IReadOnlyList<(SceneAddress Address, PropertyValue Value)> writes);

    /// <summary>Tracks a transaction submitted elsewhere (an Inspector commit, a Gizmo release).</summary>
    void Track(EditHandle handle, IReadOnlyList<(SceneAddress Address, PropertyValue Value)> writes);

    /// <summary>A tracked handle's settlement as of the last Update(); Pending for one no longer tracked.</summary>
    Settlement SettlementOf(EditHandle handle);

    /// <summary>Why a Refused handle was refused (empty otherwise).</summary>
    string RefusalText(EditHandle handle);

    /// <summary>Once per editor tick: drains the target's own failures and events.</summary>
    void Update();

    /// <summary>The invalidations observed since the last drain, in order.</summary>
    IReadOnlyList<Invalidation> DrainInvalidations();
}

public enum HistoryResult
{
    Submitted,         // the inverse (undo) or forward (redo) transaction was submitted
    NothingTo,         // the stack is empty
    Waiting,           // a history transaction is still pending
    ChangedElsewhere,  // a value differs from what the edit left there: entry dropped, nothing submitted
    Invalidated,       // an address cannot be read any more: entry dropped, nothing submitted
}

/// <summary>E2's history (ADR-0124), over any edit target. Not thread-safe.</summary>
public sealed class EditHistory
{
    public const int MaxDepth = 128;

    private enum Step
    {
        Record,
        Undo,
        Redo,
    }

    private sealed class Pending
    {
        public required EditTransaction Edit;
        public required EditHandle Handle;
        public required Step Step;
        public bool Invalidated;  // an invalidation arrived while pending: drop when settled
        public bool RedoCleared;  // an undo overtaken by a new edit: not redoable once applied
    }

    private readonly IEditTarget _target;
    private readonly List<EditTransaction> _undo = new();  // the back is the top
    private readonly List<EditTransaction> _redo = new();
    private List<Pending> _pending = new();

    public EditHistory(IEditTarget target) => _target = target;

    public bool Waiting => _pending.Count > 0;
    public bool CanUndo => !Waiting && _undo.Count > 0;
    public bool CanRedo => !Waiting && _redo.Count > 0;
    public int UndoDepth => _undo.Count;
    public int RedoDepth => _redo.Count;
    public string UndoLabel => _undo.Count == 0 ? "" : _undo[^1].Label;
    public string RedoLabel => _redo.Count == 0 ? "" : _redo[^1].Label;

    /// <summary>The last thing the history has to tell the user; empty until there is one.</summary>
    public string Notice { get; private set; } = "";

    /// <summary>
    /// A new edit, applied by <paramref name="handle"/> (submitted by its maker). Pending until Update() sees it
    /// settled: applied, undoable; refused, dropped. Clears the redo stack at once.
    /// </summary>
    public void Record(EditTransaction edit, EditHandle handle)
    {
        if (edit.Writes.Count == 0) return;
        _redo.Clear();
        foreach (Pending pending in _pending) pending.RedoCleared = true;
        _target.Track(handle, WritesOf(edit, forward: true));
        _pending.Add(new Pending { Edit = edit, Handle = handle, Step = Step.Record });
    }

    public HistoryResult Undo() => Take(_undo, Step.Undo);

    public HistoryResult Redo() => Take(_redo, Step.Redo);

    /// <summary>Once per editor tick: updates the target, settles pending transactions, applies invalidations.</summary>
    public void Update()
    {
        _target.Update();
        IReadOnlyList<Invalidation> invalidations = _target.DrainInvalidations();
        // Settle in submission order: an applied edit joins its stack before any invalidation of this tick is applied.
        var still = new List<Pending>();
        foreach (Pending pending in _pending)
        {
            foreach (Invalidation invalidation in invalidations)
            {
                if (Names(pending.Edit, invalidation)) pending.Invalidated = true;
            }
            switch (_target.SettlementOf(pending.Handle))
            {
                case Settlement.Pending:
                    still.Add(pending);
                    break;
                case Settlement.Refused:
                    Notice = "\"" + pending.Edit.Label + "\" refused: " + _target.RefusalText(pending.Handle);
                    break;
                case Settlement.Applied:
                    if (pending.Invalidated)
                    {
                        Notice = "\"" + pending.Edit.Label + "\" dropped: what it edited is gone";
                        break;
                    }
                    // An applied edit or redo is undoable; an applied undo is redoable, unless a newer edit cleared redo.
                    if (pending.Step != Step.Undo) Push(_undo, pending.Edit);
                    else if (!pending.RedoCleared) Push(_redo, pending.Edit);
                    break;
            }
        }
        _pending = still;
        foreach (Invalidation invalidation in invalidations) Invalidate(invalidation);
    }

    /// <summary>Forgets everything, pending transactions included.</summary>
    public void Clear()
    {
        _undo.Clear();
        _redo.Clear();
        _pending.Clear();
        Notice = "";
    }

    // Undo reads After where the edit left it and writes Before; redo the reverse. On a mismatch or an unreadable
    // address nothing is submitted and the entry is dropped.
    private HistoryResult Take(List<EditTransaction> from, Step kind)
    {
        if (Waiting)
        {
            Notice = "waiting for the Runtime";
            return HistoryResult.Waiting;
        }
        if (from.Count == 0) return HistoryResult.NothingTo;
        bool forward = kind == Step.Redo;
        EditTransaction edit = from[^1];
        foreach (PropertyWrite write in edit.Writes)
        {
            PropertyValue? current = _target.Read(write.Address);
            if (current is not PropertyValue value)
            {
                Notice = "\"" + edit.Label + "\" dropped: what it edited is gone";
                from.RemoveAt(from.Count - 1);
                return HistoryResult.Invalidated;
            }
            if (!value.Equals(forward ? write.Before : write.After))
            {
                Notice = "\"" + edit.Label + "\" dropped: changed elsewhere";
                from.RemoveAt(from.Count - 1);
                return HistoryResult.ChangedElsewhere;
            }
        }
        from.RemoveAt(from.Count - 1);
        EditHandle handle = _target.Submit(WritesOf(edit, forward));
        Notice = (forward ? "redo " : "undo ") + edit.Label;
        _pending.Add(new Pending { Edit = edit, Handle = handle, Step = kind });
        return HistoryResult.Submitted;
    }

    private static void Push(List<EditTransaction> stack, EditTransaction edit)
    {
        stack.Add(edit);
        while (stack.Count > MaxDepth) stack.RemoveAt(0);
    }

    private void Invalidate(Invalidation invalidation)
    {
        foreach (List<EditTransaction> stack in new[] { _undo, _redo })
        {
            int before = stack.Count;
            stack.RemoveAll(edit => Names(edit, invalidation));
            if (stack.Count != before)
            {
                Notice = "history entries dropped: " +
                         (invalidation.Component is not null ? "a component it edited was removed" : "an entity it edited was destroyed");
            }
        }
    }

    private static IReadOnlyList<(SceneAddress Address, PropertyValue Value)> WritesOf(EditTransaction edit, bool forward) =>
        edit.Writes.Select(w => (w.Address, forward ? w.After : w.Before)).ToList();

    private static bool Names(EditTransaction edit, Invalidation invalidation) =>
        edit.Writes.Any(w => w.Address.Node == invalidation.Node &&
                             (invalidation.Component is null || w.Address.Component == invalidation.Component.Value));
}
