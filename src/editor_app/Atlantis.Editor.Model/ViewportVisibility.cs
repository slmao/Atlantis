using System;
using System.Collections.Generic;
using System.Linq;
using Atlantis.Gameplay;

namespace Atlantis.Editor.Model;

public enum VisibilityAction { HideNode, HideSubtree, LockPick, Isolate, ExitIsolation, Restore }

public sealed class ViewportMembership
{
    private readonly HashSet<EntityGuid> _hidden;
    private readonly HashSet<EntityGuid> _locked;
    private readonly HashSet<EntityGuid>? _isolated;

    public ViewportMembership(ViewportPolicy? policy)
    {
        _hidden = policy?.Hidden.ToHashSet() ?? [];
        _locked = policy?.Locked.ToHashSet() ?? [];
        _isolated = policy?.Isolated?.ToHashSet();
    }

    public bool Visible(EntityGuid node) => !_hidden.Contains(node) && (_isolated?.Contains(node) != false);
    public bool Locked(EntityGuid node) => _locked.Contains(node);
    public bool Pickable(EntityGuid node) => Visible(node) && !Locked(node);

    public static (ViewportPolicy? Policy, string? Refusal) Plan(DocumentTree tree,
        ViewportPolicy? prior, VisibilityAction action, IReadOnlyList<HierarchyRow> selection)
    {
        if (action == VisibilityAction.Restore) return (null, null);
        var hidden = prior?.Hidden.ToHashSet() ?? [];
        var locked = prior?.Locked.ToHashSet() ?? [];
        var isolated = prior?.Isolated?.ToHashSet();
        if (action == VisibilityAction.ExitIsolation) isolated = null;
        else
        {
            if (selection.Count == 0 || !tree.OrderSynchronized ||
                selection.Any(row => row.Incarnation is null || !ReferenceEquals(row.Incarnation, tree.IncarnationOf(row.Node))))
                return (null, "Select current Hierarchy rows and try again.");
            var members = new HashSet<EntityGuid>();
            foreach (var root in selection)
            {
                members.Add(root.Node);
                if (action is VisibilityAction.Isolate or VisibilityAction.HideSubtree)
                    members.UnionWith(tree.DescendantsOf(root.Node));
                if (members.Count > ViewportPolicy.MaxEntries)
                    return (null, "The selected subtrees exceed the viewport member limit.");
            }
            if (action is VisibilityAction.HideNode or VisibilityAction.HideSubtree) hidden.UnionWith(members);
            if (action == VisibilityAction.LockPick) locked.UnionWith(members);
            if (action == VisibilityAction.Isolate) isolated = members;
        }
        if (hidden.Count + locked.Count + (isolated?.Count ?? 0) > ViewportPolicy.MaxEntries)
            return (null, "The combined viewport sets exceed the viewport member limit.");
        if (hidden.Count == 0 && locked.Count == 0 && isolated is null) return (null, null);
        static EntityGuid[] Ordered(IEnumerable<EntityGuid> ids) => ids.OrderBy(id => id.ToString(), StringComparer.Ordinal).ToArray();
        return (new(Ordered(hidden), Ordered(locked), isolated is null ? null : Ordered(isolated)), null);
    }
}
