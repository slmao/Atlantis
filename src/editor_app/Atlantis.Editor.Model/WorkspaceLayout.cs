using System;

namespace Atlantis.Editor.Model;

public enum WorkspaceTab { Inspector, Assets, SavedScenes }

public readonly record struct WorkspaceSideWidths(double Hierarchy, double Right)
{
    public bool IsValid => double.IsFinite(Hierarchy) && double.IsFinite(Right) &&
        Hierarchy > 0 && Right > 0 && Hierarchy <= 32768 && Right <= 32768;
}

public readonly record struct WorkspacePaneWidths(double Hierarchy, double Viewport, double Right);

public readonly record struct WorkspaceLayout(WorkspaceSideWidths? Preferred, WorkspaceTab? Tab)
{
    public const double MinimumHierarchy = 173, MinimumViewport = 280, MinimumRight = 298;
    public const double SplitterWidth = 5;

    public WorkspaceTab AvailableTab(bool savedScenes) => Tab switch
    {
        WorkspaceTab.Assets => WorkspaceTab.Assets,
        WorkspaceTab.SavedScenes when savedScenes => WorkspaceTab.SavedScenes,
        _ => WorkspaceTab.Inspector,
    };

    public WorkspacePaneWidths? Project(double measuredWidth)
    {
        double available = measuredWidth - 2 * SplitterWidth;
        double budget = available - MinimumHierarchy - MinimumViewport - MinimumRight;
        if (!double.IsFinite(budget) || budget < 0) return null;

        WorkspaceSideWidths preferred = Preferred is { IsValid: true } saved ? saved :
            measuredWidth < 1100 ? new(173, 308) : new(220, 368);
        double leftExtra = Math.Max(0, preferred.Hierarchy - MinimumHierarchy);
        double rightExtra = Math.Max(0, preferred.Right - MinimumRight);
        double requested = leftExtra + rightExtra;
        double hierarchy = MinimumHierarchy + leftExtra;
        double right = MinimumRight + rightExtra;
        if (requested > budget)
        {
            double left = budget * (leftExtra / requested);
            hierarchy = MinimumHierarchy + left;
            right = MinimumRight + (budget - left);
        }
        return new(hierarchy, Math.Max(MinimumViewport, available - hierarchy - right), right);
    }
}
