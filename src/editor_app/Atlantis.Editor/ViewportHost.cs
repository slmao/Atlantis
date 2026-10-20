// Spec 0064 Phase 1 (Plan 0064 P4; ADR-0126 V-a): the Viewport pane -- an
// Avalonia NativeControlHost whose native control is the editor's
// ViewportWindow, the Runtime's --parent-window.
using System;
using Avalonia.Controls;
using Avalonia.Platform;

namespace Atlantis.Editor;

public sealed class ViewportHost : NativeControlHost
{
    public ViewportWindow? Window { get; private set; }

    /// <summary>Raised once the host window exists (its handle is the Runtime's parent).</summary>
    public event Action<ViewportWindow>? WindowCreated;

    /// <summary>Raised when Avalonia destroys the host window (the Runtime's child goes with it).</summary>
    public event Action? WindowDestroyed;

    protected override IPlatformHandle CreateNativeControlCore(IPlatformHandle parent)
    {
        if (!OperatingSystem.IsWindows()) return base.CreateNativeControlCore(parent);
        ViewportWindow window = ViewportWindow.Create(parent.Handle);
        Window = window;
        WindowCreated?.Invoke(window);
        return new PlatformHandle(window.Handle, "HWND");
    }

    protected override void DestroyNativeControlCore(IPlatformHandle control)
    {
        if (Window is not null && control.Handle == Window.Handle)
        {
            Window.Dispose();
            Window = null;
            WindowDestroyed?.Invoke();
            return;
        }
        base.DestroyNativeControlCore(control);
    }
}
