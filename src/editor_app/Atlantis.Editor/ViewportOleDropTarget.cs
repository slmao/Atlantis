using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.Marshalling;
using Atlantis.Editor.Model;
using Avalonia.Threading;

namespace Atlantis.Editor;

[StructLayout(LayoutKind.Sequential)]
internal record struct OlePoint(int X, int Y);

[StructLayout(LayoutKind.Sequential)]
internal struct OleFormat
{
    internal ushort Format;
    internal nint Device;
    internal int Aspect, Index;
    internal uint Medium;
}

[StructLayout(LayoutKind.Sequential)]
internal struct OleStorage
{
    internal uint Kind;
    internal nint Data, ReleaseOwner;
}

[GeneratedComInterface(Options = ComInterfaceOptions.ComObjectWrapper)]
[Guid("0000010e-0000-0000-C000-000000000046")]
internal partial interface IOleDragData
{
    [PreserveSig] int GetData(ref OleFormat format, out OleStorage storage);
}

[GeneratedComInterface(Options = ComInterfaceOptions.ManagedObjectWrapper)]
[Guid("00000122-0000-0000-C000-000000000046")]
internal partial interface IOleViewportDrop
{
    [PreserveSig] int DragEnter(IOleDragData data, uint keys, OlePoint point, ref uint effect);
    [PreserveSig] int DragOver(uint keys, OlePoint point, ref uint effect);
    [PreserveSig] int DragLeave();
    [PreserveSig] int Drop(IOleDragData data, uint keys, OlePoint point, ref uint effect);
}

[GeneratedComClass]
// UI-thread-owned. OLE may reenter callbacks on that thread during the drag loop.
internal sealed partial class ViewportOleDropTarget : IOleViewportDrop, IDisposable
{
    internal const string TokenFormat = "Atlantis.Editor.AssetDrag.v1";
    private const string WindowClass = "AtlantisEditorViewportOleTarget";
    private const uint Copy = 1, GlobalMemory = 1;
    private const uint Layered = 0x00080000, ChildWindow = 0x40000000, ClipSiblings = 0x04000000;
    private const uint ConstantAlpha = 2, NoActivate = 0x0010, Show = 0x0040, MouseActivate = 0x0021;
    private const int Hide = 0, DoNotActivate = 3, BlackBrush = 4, ContentAspect = 1;
    private static readonly StrategyBasedComWrappers Wrappers = new();
    private static readonly WindowProc Procedure = WindowMessage;
    private static bool s_windowClass;

    private readonly ViewportWindow _viewport;
    private readonly nint _parent, _runtime;
    private readonly int _width, _height;
    private readonly AssetDragIntent _intent;
    private readonly Func<string, nint, int, int, bool> _notify;
    private readonly Action<string> _log;
    private readonly ushort _format;
    private nint _window;
    private bool _initialized, _registered, _active, _matched, _retiring;
    private int _callbackDepth;

    private ViewportOleDropTarget(ViewportWindow viewport, AssetDragIntent intent,
        Func<string, nint, int, int, bool> notify, Action<string> log)
    {
        _viewport = viewport;
        _parent = viewport.Handle;
        _runtime = viewport.RuntimeChild;
        _width = viewport.Width;
        _height = viewport.Height;
        _intent = intent;
        _notify = notify;
        _log = log;
        uint format = RegisterClipboardFormatW(TokenFormat);
        if (format == 0 || format > ushort.MaxValue) throw WindowError("RegisterClipboardFormat");
        _format = (ushort)format;
    }

    internal static ViewportOleDropTarget Create(ViewportWindow viewport, AssetDragIntent intent,
        Func<string, nint, int, int, bool> notify, Action<string> log,
        Func<nint, nint, int>? register = null)
    {
        Dispatcher.UIThread.VerifyAccess();
        var target = new ViewportOleDropTarget(viewport, intent, notify, log);
        try { target.Arm(register ?? RegisterDragDrop); return target; }
        catch { target.Dispose(); throw; }
    }

    private void Arm(Func<nint, nint, int> register)
    {
        if (!DrawableCurrent()) throw new InvalidOperationException("Viewport drawable is not ready.");
        Marshal.ThrowExceptionForHR(OleInitialize(0));
        _initialized = true;

        if (!s_windowClass)
        {
            var windowClass = new WindowClassData
            {
                Size = (uint)Marshal.SizeOf<WindowClassData>(),
                Procedure = Marshal.GetFunctionPointerForDelegate(Procedure),
                Instance = GetModuleHandleW(null),
                Background = GetStockObject(BlackBrush),
                Name = WindowClass
            };
            if (RegisterClassExW(ref windowClass) == 0) throw WindowError("RegisterClassEx");
            s_windowClass = true;
        }
        _window = CreateWindowExW(Layered, WindowClass, "", ChildWindow | ClipSiblings,
            0, 0, _width, _height, _parent, 0, GetModuleHandleW(null), 0);
        if (_window == 0) throw WindowError("CreateWindowEx");
        // Zero opacity would make the layered window pass through hit tests.
        if (!SetLayeredWindowAttributes(_window, 0, 1, ConstantAlpha)) throw WindowError("SetLayeredWindowAttributes");
        nint wrapper = Wrappers.GetOrCreateComInterfaceForObject(this, CreateComInterfaceFlags.None);
        try
        {
            Guid targetInterface = typeof(IOleViewportDrop).GUID;
            Marshal.ThrowExceptionForHR(Marshal.QueryInterface(wrapper, in targetInterface, out nint target));
            try { Marshal.ThrowExceptionForHR(register(_window, target)); _registered = true; }
            finally { Marshal.Release(target); }
        }
        finally { Marshal.Release(wrapper); }
        if (!SetWindowPos(_window, 0, 0, 0, _width, _height, NoActivate | Show))
            throw WindowError("SetWindowPos");
        _active = true;
        _notify("arm", _window, _width, _height);
    }

    private bool DrawableCurrent() => _width > 0 && _height > 0 &&
        _viewport.Handle == _parent && _viewport.RuntimeChild == _runtime && _viewport.RuntimeChildIsHosted() &&
        _viewport.Width == _width && _viewport.Height == _height &&
        GetWindowRect(_parent, out NativeRect host) && GetWindowRect(_runtime, out NativeRect child) && child.Equals(host);

    private bool Matches(IOleDragData data)
    {
        var format = new OleFormat { Format = _format, Aspect = ContentAspect, Index = -1, Medium = GlobalMemory };
        int result = data.GetData(ref format, out OleStorage storage);
        if (result < 0) { _log($"viewport OLE token unavailable: 0x{result:X8}"); return false; }
        try
        {
            if (storage.Kind != GlobalMemory || storage.Data == 0 || GlobalSize(storage.Data) != 16)
            {
                _log($"viewport OLE unexpected token medium {storage.Kind} or size");
                return false;
            }
            nint bytes = GlobalLock(storage.Data);
            if (bytes == 0) return false;
            try
            {
                var token = new byte[16];
                Marshal.Copy(bytes, token, 0, token.Length);
                return _intent.Matches(Convert.ToHexStringLower(token));
            }
            finally { GlobalUnlock(storage.Data); }
        }
        finally { ReleaseStgMedium(ref storage); }
    }

    public int DragEnter(IOleDragData data, uint keys, OlePoint point, ref uint effect) =>
        PointCallback("enter", point, ref effect, data);
    public int DragOver(uint keys, OlePoint point, ref uint effect) =>
        PointCallback("over", point, ref effect);
    public int Drop(IOleDragData data, uint keys, OlePoint point, ref uint effect) =>
        PointCallback("drop", point, ref effect, data);

    private int PointCallback(string phase, OlePoint point, ref uint effect, IOleDragData? data = null)
    {
        uint allowed = effect;
        effect = 0;
        ++_callbackDepth;
        try
        {
            if (!_active || !DrawableCurrent()) { Dispose(); return 0; }
            if (data is not null) _matched = Matches(data);
            if (!_active || !_matched || !_intent.Matches(_intent.Token) ||
                (allowed & Copy) == 0 || WindowFromPoint(point) != _window || !ScreenToClient(_parent, ref point) ||
                point.X < 0 || point.Y < 0 || point.X >= _width || point.Y >= _height) return 0;
            if (_notify(phase, _window, point.X, point.Y)) effect = Copy;
        }
        catch (Exception error) { _log("viewport OLE " + phase + ": " + error.Message); Dispose(); }
        finally
        {
            if (phase == "drop") Dispose();
            CompleteCallback();
        }
        return 0;
    }

    public int DragLeave()
    {
        ++_callbackDepth;
        try { if (_active && _matched) _notify("leave", _window, -1, -1); }
        catch (Exception error) { _log("viewport OLE leave: " + error.Message); Dispose(); }
        finally { _matched = false; CompleteCallback(); }
        return 0;
    }

    public void Dispose()
    {
        Dispatcher.UIThread.VerifyAccess();
        bool wasActive = _active;
        _active = _matched = false;
        if (_window != 0) ShowWindow(_window, Hide);
        if (wasActive) _notify("disarm", _window, -1, -1);
        _retiring = true;
        if (_callbackDepth > 0) return;
        Finish();
    }

    private void CompleteCallback()
    {
        if (--_callbackDepth == 0 && _retiring) Dispatcher.UIThread.Post(Finish);
    }

    private void Finish()
    {
        if (!_retiring || _callbackDepth != 0) return;
        _retiring = false;
        nint window = _window;
        _window = 0;
        if (_registered)
        {
            _registered = false;
            int result = RevokeDragDrop(window);
            if (result < 0) _log($"viewport OLE revoke: 0x{result:X8}");
        }
        if (window != 0 && IsWindow(window) && !DestroyWindow(window))
            _log("viewport OLE destroy: " + Marshal.GetLastWin32Error());
        if (_initialized) { _initialized = false; OleUninitialize(); }
        if (window != 0) _notify("closed", window, -1, -1);
    }

    private static nint WindowMessage(nint window, uint message, nint wParam, nint lParam) =>
        message == MouseActivate ? DoNotActivate : DefWindowProcW(window, message, wParam, lParam);

    private static Win32Exception WindowError(string call) => new(Marshal.GetLastWin32Error(), call);
    private delegate nint WindowProc(nint window, uint message, nint wParam, nint lParam);
    [StructLayout(LayoutKind.Sequential)] private record struct NativeRect(int Left, int Top, int Right, int Bottom);
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct WindowClassData
    {
        internal uint Size, Style;
        internal nint Procedure;
        internal int ClassExtra, WindowExtra;
        internal nint Instance, Icon, Cursor, Background;
        internal string? Menu, Name;
        internal nint SmallIcon;
    }

    [DllImport("ole32")] private static extern int OleInitialize(nint reserved);
    [DllImport("ole32")] private static extern void OleUninitialize();
    [DllImport("ole32")] private static extern int RegisterDragDrop(nint window, nint target);
    [DllImport("ole32")] private static extern int RevokeDragDrop(nint window);
    [DllImport("ole32")] private static extern void ReleaseStgMedium(ref OleStorage storage);
    [DllImport("kernel32", CharSet = CharSet.Unicode)] private static extern nint GetModuleHandleW(string? name);
    [DllImport("kernel32")] private static extern nuint GlobalSize(nint memory);
    [DllImport("kernel32")] private static extern nint GlobalLock(nint memory);
    [DllImport("kernel32")] private static extern bool GlobalUnlock(nint memory);
    [DllImport("gdi32")] private static extern nint GetStockObject(int index);
    [DllImport("user32", CharSet = CharSet.Unicode, SetLastError = true)] private static extern uint RegisterClipboardFormatW(string name);
    [DllImport("user32", CharSet = CharSet.Unicode, SetLastError = true)] private static extern ushort RegisterClassExW(ref WindowClassData windowClass);
    [DllImport("user32", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern nint CreateWindowExW(uint extendedStyle, string className, string title, uint style,
        int x, int y, int width, int height, nint parent, nint menu, nint instance, nint parameter);
    [DllImport("user32")] private static extern nint DefWindowProcW(nint window, uint message, nint wParam, nint lParam);
    [DllImport("user32", SetLastError = true)] private static extern bool SetLayeredWindowAttributes(nint window, uint color, byte alpha, uint flags);
    [DllImport("user32", SetLastError = true)] private static extern bool SetWindowPos(nint window, nint after, int x, int y, int w, int h, uint flags);
    [DllImport("user32")] private static extern bool GetWindowRect(nint window, out NativeRect rect);
    [DllImport("user32")] private static extern bool ScreenToClient(nint window, ref OlePoint point);
    [DllImport("user32")] private static extern nint WindowFromPoint(OlePoint point);
    [DllImport("user32")] private static extern bool IsWindow(nint window);
    [DllImport("user32")] private static extern bool ShowWindow(nint window, int command);
    [DllImport("user32", SetLastError = true)] private static extern bool DestroyWindow(nint window);
}
