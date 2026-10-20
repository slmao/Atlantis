// Spec 0064 Phase 1 (Plan 0064 P4; ADR-0126 D1/D4 route R-a; Plan 0064 J5):
// the Viewport's host window -- an editor-owned Win32 child window inside
// Avalonia's NativeControlHost. Its handle is atlantis_runtime's
// --parent-window: the Runtime creates its own child window inside it, and
// in child-window mode re-posts pointer, wheel and key messages to it (route
// R-a), which this window's procedure turns into events. The editor sizes the
// Runtime's child with SetWindowPos (asynchronously: the Runtime's thread may
// be inside a 130 ms Bistro frame). user32 calls on the editor's own windows
// and the hosted child only -- no engine code is called.
using System;
using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;

namespace Atlantis.Editor;

public enum ViewportPointer
{
    Move,
    LeftDown,
    LeftUp,
    RightDown,
    RightUp,
    MiddleDown,
    MiddleUp,
}

/// <summary>A forwarded pointer message, in the host window's client pixels, with its arrival time.</summary>
public readonly record struct ViewportPointerEvent(ViewportPointer Kind, int X, int Y, bool Alt, bool Shift, long ArrivalTicks);

public sealed class ViewportWindow : IDisposable
{
    private const string ClassName = "AtlantisEditorViewportHost";
    private static WndProc? s_wndProc;  // kept alive for the class's lifetime
    private static bool s_registered;
    private static readonly System.Collections.Generic.Dictionary<IntPtr, ViewportWindow> s_windows = new();

    private ViewportWindow(IntPtr handle) => Handle = handle;

    public IntPtr Handle { get; private set; }

    /// <summary>The Runtime's child window, once the session file names it.</summary>
    public IntPtr RuntimeChild { get; set; }

    public int Width { get; private set; }
    public int Height { get; private set; }

    public event Action<int, int>? Resized;
    public event Action<ViewportPointerEvent>? Pointer;
    public event Action<int, long>? Wheel;          // notches x 120 (WHEEL_DELTA), arrival ticks
    public event Action<int, bool, long>? Key;      // virtual key, down, arrival ticks
    public event Action<char>? Character;

    public static ViewportWindow Create(IntPtr parent)
    {
        Register();
        GetClientRect(parent, out RECT rect);
        IntPtr hwnd = CreateWindowExW(0, ClassName, "", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0,
                                      Math.Max(1, rect.Right - rect.Left), Math.Max(1, rect.Bottom - rect.Top), parent,
                                      IntPtr.Zero, GetModuleHandleW(null), IntPtr.Zero);
        if (hwnd == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error(), "CreateWindowEx (viewport host)");
        var window = new ViewportWindow(hwnd);
        s_windows[hwnd] = window;
        GetClientRect(hwnd, out RECT client);
        window.Width = client.Right - client.Left;
        window.Height = client.Bottom - client.Top;
        return window;
    }

    /// <summary>Sizes the Runtime's child to this window's client area, without waiting for the Runtime's thread.</summary>
    public void FitRuntimeChild()
    {
        if (RuntimeChild == IntPtr.Zero || !IsWindow(RuntimeChild)) return;
        SetWindowPos(RuntimeChild, IntPtr.Zero, 0, 0, Math.Max(1, Width), Math.Max(1, Height),
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
    }

    /// <summary>Whether the Runtime's child is a child of this window (WS_CHILD, parented here).</summary>
    public bool RuntimeChildIsHosted() =>
        RuntimeChild != IntPtr.Zero && IsWindow(RuntimeChild) && GetParent(RuntimeChild) == Handle &&
        (GetWindowLongPtrW(RuntimeChild, GWL_STYLE).ToInt64() & WS_CHILD) != 0;

    public void Dispose()
    {
        if (Handle == IntPtr.Zero) return;
        s_windows.Remove(Handle);
        if (IsWindow(Handle)) DestroyWindow(Handle);
        Handle = IntPtr.Zero;
    }

    private static void Register()
    {
        if (s_registered) return;
        s_wndProc = Procedure;
        var wc = new WNDCLASSEXW
        {
            cbSize = (uint)Marshal.SizeOf<WNDCLASSEXW>(),
            style = CS_DBLCLKS,
            lpfnWndProc = Marshal.GetFunctionPointerForDelegate(s_wndProc),
            hInstance = GetModuleHandleW(null),
            hCursor = LoadCursorW(IntPtr.Zero, IDC_ARROW),
            hbrBackground = GetStockObject(BLACK_BRUSH),
            lpszClassName = ClassName,
        };
        if (RegisterClassExW(ref wc) == 0) throw new Win32Exception(Marshal.GetLastWin32Error(), "RegisterClassEx (viewport host)");
        s_registered = true;
    }

    private static IntPtr Procedure(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam)
    {
        if (!s_windows.TryGetValue(hwnd, out ViewportWindow? window)) return DefWindowProcW(hwnd, message, wParam, lParam);
        long now = Stopwatch.GetTimestamp();
        int x = (short)(lParam.ToInt64() & 0xFFFF);
        int y = (short)((lParam.ToInt64() >> 16) & 0xFFFF);
        bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        switch (message)
        {
            case WM_SIZE:
                window.Width = x & 0xFFFF;
                window.Height = y & 0xFFFF;
                window.FitRuntimeChild();
                window.Resized?.Invoke(window.Width, window.Height);
                return IntPtr.Zero;
            case WM_ERASEBKGND:
                return new IntPtr(1);
            case WM_MOUSEMOVE: window.Pointer?.Invoke(new(ViewportPointer.Move, x, y, alt, shift, now)); return IntPtr.Zero;
            case WM_LBUTTONDOWN: window.Pointer?.Invoke(new(ViewportPointer.LeftDown, x, y, alt, shift, now)); return IntPtr.Zero;
            case WM_LBUTTONUP: window.Pointer?.Invoke(new(ViewportPointer.LeftUp, x, y, alt, shift, now)); return IntPtr.Zero;
            case WM_RBUTTONDOWN: window.Pointer?.Invoke(new(ViewportPointer.RightDown, x, y, alt, shift, now)); return IntPtr.Zero;
            case WM_RBUTTONUP: window.Pointer?.Invoke(new(ViewportPointer.RightUp, x, y, alt, shift, now)); return IntPtr.Zero;
            case WM_MBUTTONDOWN: window.Pointer?.Invoke(new(ViewportPointer.MiddleDown, x, y, alt, shift, now)); return IntPtr.Zero;
            case WM_MBUTTONUP: window.Pointer?.Invoke(new(ViewportPointer.MiddleUp, x, y, alt, shift, now)); return IntPtr.Zero;
            case WM_MOUSEWHEEL:
                window.Wheel?.Invoke((short)((wParam.ToInt64() >> 16) & 0xFFFF), now);
                return IntPtr.Zero;
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN:
                window.Key?.Invoke((int)wParam.ToInt64(), true, now);
                return IntPtr.Zero;
            case WM_KEYUP:
            case WM_SYSKEYUP:
                window.Key?.Invoke((int)wParam.ToInt64(), false, now);
                return IntPtr.Zero;
            case WM_CHAR:
                window.Character?.Invoke((char)wParam.ToInt64());
                return IntPtr.Zero;
            case WM_DESTROY:
                s_windows.Remove(hwnd);
                break;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    // --- user32 / gdi32 / kernel32.

    private delegate IntPtr WndProc(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam);

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct WNDCLASSEXW
    {
        public uint cbSize;
        public uint style;
        public IntPtr lpfnWndProc;
        public int cbClsExtra;
        public int cbWndExtra;
        public IntPtr hInstance;
        public IntPtr hIcon;
        public IntPtr hCursor;
        public IntPtr hbrBackground;
        public string? lpszMenuName;
        public string lpszClassName;
        public IntPtr hIconSm;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct RECT
    {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    private const uint WS_CHILD = 0x40000000;
    private const uint WS_VISIBLE = 0x10000000;
    private const uint WS_CLIPCHILDREN = 0x02000000;
    private const uint WS_CLIPSIBLINGS = 0x04000000;
    private const uint CS_DBLCLKS = 0x0008;
    private const int GWL_STYLE = -16;
    private const int BLACK_BRUSH = 4;
    private static readonly IntPtr IDC_ARROW = new(32512);
    private const uint SWP_NOZORDER = 0x0004;
    private const uint SWP_NOACTIVATE = 0x0010;
    private const uint SWP_ASYNCWINDOWPOS = 0x4000;
    private const int VK_SHIFT = 0x10;
    private const int VK_MENU = 0x12;
    private const uint WM_DESTROY = 0x0002;
    private const uint WM_SIZE = 0x0005;
    private const uint WM_ERASEBKGND = 0x0014;
    private const uint WM_KEYDOWN = 0x0100;
    private const uint WM_KEYUP = 0x0101;
    private const uint WM_CHAR = 0x0102;
    private const uint WM_SYSKEYDOWN = 0x0104;
    private const uint WM_SYSKEYUP = 0x0105;
    private const uint WM_MOUSEMOVE = 0x0200;
    private const uint WM_LBUTTONDOWN = 0x0201;
    private const uint WM_LBUTTONUP = 0x0202;
    private const uint WM_RBUTTONDOWN = 0x0204;
    private const uint WM_RBUTTONUP = 0x0205;
    private const uint WM_MBUTTONDOWN = 0x0207;
    private const uint WM_MBUTTONUP = 0x0208;
    private const uint WM_MOUSEWHEEL = 0x020A;

    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern ushort RegisterClassExW(ref WNDCLASSEXW wc);

    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr CreateWindowExW(uint exStyle, string className, string windowName, uint style, int x, int y,
                                                 int width, int height, IntPtr parent, IntPtr menu, IntPtr instance, IntPtr param);

    [DllImport("user32.dll")]
    private static extern IntPtr DefWindowProcW(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool DestroyWindow(IntPtr hwnd);

    [DllImport("user32.dll")]
    private static extern bool IsWindow(IntPtr hwnd);

    [DllImport("user32.dll")]
    private static extern IntPtr GetParent(IntPtr hwnd);

    [DllImport("user32.dll")]
    private static extern IntPtr GetWindowLongPtrW(IntPtr hwnd, int index);

    [DllImport("user32.dll")]
    private static extern bool GetClientRect(IntPtr hwnd, out RECT rect);

    [DllImport("user32.dll")]
    private static extern bool SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int cx, int cy, uint flags);

    [DllImport("user32.dll")]
    private static extern short GetKeyState(int key);

    [DllImport("user32.dll")]
    private static extern IntPtr LoadCursorW(IntPtr instance, IntPtr cursor);

    [DllImport("gdi32.dll")]
    private static extern IntPtr GetStockObject(int index);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    private static extern IntPtr GetModuleHandleW(string? moduleName);
}
