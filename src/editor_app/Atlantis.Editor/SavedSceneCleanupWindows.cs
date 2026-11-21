using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Runtime.Versioning;
using System.Security.AccessControl;
using System.Security.Principal;
using System.Text;
using System.Threading.Tasks;
using Microsoft.Win32.SafeHandles;

namespace Atlantis.Editor;

[SupportedOSPlatform("windows")]
internal static class SavedSceneCleanupWindows
{
    internal readonly record struct FileIdentity(uint Volume, ulong File);
    internal readonly record struct Entry(string RelativePath, bool Directory, FileIdentity Identity, long Length, long LastWrite);

    // Open each path component before using it to resolve the next one. Keep the
    // ancestors pinned against renames and refuse reparse points at open time.
    internal sealed class PinnedDirectories : IDisposable
    {
        private readonly List<SafeFileHandle> _handles = [];
        internal FileIdentity Add(string path)
        {
            var directories = new Stack<string>();
            for (string? current = Path.GetFullPath(path); current is not null; current = Path.GetDirectoryName(current))
                directories.Push(current);
            var drive = new DriveInfo(directories.Peek());
            if (drive.DriveType != DriveType.Fixed || drive.DriveFormat != "NTFS")
                throw new IOException("Cleanup requires a local NTFS directory.");
            FileIdentity identity = default;
            foreach (string directory in directories)
            {
                var handle = Open(directory, directory: true, deleting: false);
                _handles.Add(handle);
                identity = Describe(handle, "", directory: true).Identity;
            }
            return identity;
        }

        public void Dispose()
        {
            foreach (var handle in _handles) handle.Dispose();
        }
    }

    internal sealed class BuildTree : IDisposable
    {
        private const int MaxEntries = 100_000;
        private readonly string _root;
        private readonly List<(Entry Entry, SafeFileHandle Handle)> _entries = [];
        internal Entry[] Inventory { get; }

        internal BuildTree(string root, bool deleting)
        {
            _root = root;
            try
            {
                Scan(root, "", directory: true, deleting, depth: 0);
                Inventory = _entries.Select(item => item.Entry).ToArray();
                VerifyMembership();
            }
            catch
            {
                Dispose();
                throw;
            }
        }

        private void Scan(string path, string relative, bool directory, bool deleting, int depth)
        {
            if (_entries.Count >= MaxEntries || depth > 128) throw new IOException("Build folder exceeds the inspection limit.");
            var handle = Open(path, directory, deleting);
            Entry entry;
            try { entry = Describe(handle, relative, directory); }
            catch { handle.Dispose(); throw; }
            _entries.Add((entry, handle));
            if (!directory) return;
            foreach (string child in Directory.EnumerateFileSystemEntries(path).Order(StringComparer.Ordinal))
            {
                bool isDirectory = File.GetAttributes(child).HasFlag(FileAttributes.Directory);
                Scan(child, Path.GetRelativePath(_root, child), isDirectory, deleting, depth + 1);
            }
        }

        internal void VerifyMembership()
        {
            var expected = _entries.Select(item => item.Entry.RelativePath).ToHashSet(StringComparer.Ordinal);
            foreach (var (entry, handle) in _entries)
            {
                if (Describe(handle, entry.RelativePath, entry.Directory) != entry)
                    throw new IOException("The build folder changed during inspection.");
                if (!entry.Directory) continue;
                foreach (string child in Directory.EnumerateFileSystemEntries(Path.Combine(_root, entry.RelativePath)))
                    if (!expected.Remove(Path.GetRelativePath(_root, child)))
                        throw new IOException("The build folder membership changed during inspection.");
            }
            if (expected.Count != 1 || !expected.Contains(""))
                throw new IOException("The build folder membership changed during inspection.");
        }

        public void Dispose()
        {
            foreach (var item in _entries) item.Handle.Dispose();
        }
    }

    internal sealed record Permission(Entry Directory, byte[] Original, byte[] Sealed);

    // Directory share modes do not prevent creation of a new child by name.
    // The live handle set and each isolated directory's own DACL are both needed.
    internal sealed class IsolatedTree : IDisposable
    {
        private readonly List<(Entry Entry, SafeFileHandle Handle)> _entries = [];
        private bool _sealed;
        internal Entry[] Inventory => _entries.Select(item => item.Entry).ToArray();

        internal IsolatedTree(string path, FileIdentity identity)
        {
            try
            {
                // MAXIMUM_ALLOWED prevents SetSecurityInfo from propagating ACLs.
                var root = CreateFile(path, 0x02000000, 0, IntPtr.Zero, 3, 0x02200000, IntPtr.Zero);
                if (root.IsInvalid) { root.Dispose(); throw Win32("Cannot exclusively open the isolated build"); }
                Scan(root, "", directory: true, depth: 0);
                if (_entries[0].Entry.Identity != identity) throw new IOException("Isolated build identity changed.");
            }
            catch { Dispose(); throw; }
        }

        private void Scan(SafeFileHandle handle, string relative, bool directory, int depth)
        {
            Entry entry;
            try
            {
                if (_entries.Count >= 100_000 || depth > 128) throw new IOException("Build folder exceeds the inspection limit.");
                entry = Describe(handle, relative, directory);
            }
            catch { handle.Dispose(); throw; }
            _entries.Add((entry, handle));
            if (!directory) return;
            foreach (var child in ReadChildren(handle).OrderBy(child => child.Name, StringComparer.Ordinal))
                Scan(OpenChild(handle, child.Name, child.Directory), Path.Combine(relative, child.Name), child.Directory, depth + 1);
        }

        internal Permission[] CapturePermissions() => _entries.Where(item => item.Entry.Directory).Select(item =>
        {
            byte[] original = ReadSecurity(item.Handle);
            return new Permission(item.Entry, original, SealedDescriptor(original));
        }).ToArray();

        private void ChangePermissions(Permission[] permissions, bool seal, Action? afterEach)
        {
            var directories = _entries.Where(item => item.Entry.Directory).ToDictionary(item => item.Entry.RelativePath, StringComparer.Ordinal);
            var updates = new List<(SafeFileHandle Handle, byte[] Desired)>();
            foreach (var permission in permissions)
            {
                if (!directories.TryGetValue(permission.Directory.RelativePath, out var target))
                    throw new IOException("A directory recorded for permission recovery is missing.");
                if (target.Entry.Identity != permission.Directory.Identity ||
                    !SealedDescriptor(permission.Original).SequenceEqual(permission.Sealed))
                    throw new IOException("A directory recorded for permission recovery changed identity or descriptor.");
                byte[] current = ReadSecurity(target.Handle);
                if (!current.SequenceEqual(permission.Original) && !current.SequenceEqual(permission.Sealed))
                    throw new IOException("A directory's permissions changed. The isolated build was retained.");
                byte[] desired = seal ? permission.Sealed : permission.Original;
                if (!current.SequenceEqual(desired)) updates.Add((target.Handle, desired));
            }
            if (seal && (permissions.Length != directories.Count ||
                        permissions.Select(permission => permission.Directory.RelativePath).Distinct(StringComparer.Ordinal).Count() != directories.Count))
                throw new IOException("A directory has no recorded permission guard.");
            foreach (var (handle, desired) in updates)
            {
                WriteSecurity(handle, desired);
                if (!ReadSecurity(handle).SequenceEqual(desired))
                    throw new IOException("Windows did not preserve the required security descriptor.");
                afterEach?.Invoke();
            }
        }

        internal void Seal(Permission[] permissions, Action? afterEach)
        {
            ChangePermissions(permissions, seal: true, afterEach);
            _sealed = true;
        }

        internal void Restore(Permission[] permissions)
        {
            _sealed = false;
            ChangePermissions(permissions, seal: false, afterEach: null);
        }

        internal void VerifySealed(Permission[] permissions)
        {
            if (!_sealed || _entries.Any(item => item.Handle.IsClosed)) throw new IOException("The build directory guard was lost.");
            var sealedDirectories = permissions.ToDictionary(permission => permission.Directory.RelativePath, StringComparer.Ordinal);
            var expected = _entries.Select(item => item.Entry).Where(item => item.RelativePath != "").ToLookup(
                item => Path.GetDirectoryName(item.RelativePath)!, item => (Path.GetFileName(item.RelativePath), item.Directory), StringComparer.Ordinal);
            foreach (var (entry, handle) in _entries)
            {
                if (Describe(handle, entry.RelativePath, entry.Directory) != entry)
                    throw new IOException("The build folder changed during isolation.");
                if (!entry.Directory) continue;
                if (!ReadSecurity(handle).SequenceEqual(sealedDirectories[entry.RelativePath].Sealed))
                    throw new IOException("The build directory guard changed.");
                if (!ReadChildren(handle).ToHashSet().SetEquals(expected[entry.RelativePath]))
                    throw new IOException("The isolated build membership changed.");
            }
        }

        internal void Delete(Permission[] permissions, Action? disposition, Action? removed)
        {
            VerifySealed(permissions);
            for (int index = _entries.Count - 1; index >= 0; --index)
            {
                var (entry, handle) = _entries[index];
                if (entry.Directory && ReadChildren(handle).Count != 0)
                    throw new IOException("The build folder membership changed during removal.");
                byte requested = 1;
                disposition?.Invoke();
                if (!SetFileInformationByHandle(handle, 4, ref requested, 1)) throw Win32("Cannot remove " + entry.RelativePath);
                handle.Dispose();
                removed?.Invoke();
            }
        }

        public void Dispose()
        {
            _sealed = false;
            foreach (var item in _entries) item.Handle.Dispose();
        }
    }

    internal static void MoveAttempt(string source, string destination, FileIdentity identity)
    {
        if (Path.GetDirectoryName(source) != Path.GetDirectoryName(destination))
            throw new IOException("The isolated location must be in the same managed attempt area.");
        using var handle = Open(source, directory: true, deleting: true);
        if (Describe(handle, "", directory: true).Identity != identity)
            throw new IOException("The build directory identity changed before the rename.");

        var name = Encoding.Unicode.GetBytes(destination);
        int lengthOffset = IntPtr.Size * 2, nameOffset = lengthOffset + sizeof(int);
        var rename = new byte[nameOffset + name.Length + sizeof(char)];
        System.Buffers.Binary.BinaryPrimitives.WriteInt32LittleEndian(rename.AsSpan(lengthOffset), name.Length);
        name.CopyTo(rename, nameOffset);
        if (!SetFileInformationByHandle(handle, 3, rename, rename.Length)) throw Win32("Cannot rename the verified build directory");
    }

    internal static SafeFileHandle HoldVacantOriginal(string path)
    {
        // CREATE_NEW will not open or replace a directory or file that won
        // the original name. The private placeholder is held to delete-on-close.
        const uint delete = 0x00010000, createNew = 1, deleteOnClose = 0x04000000, noFollow = 0x00200000;
        var handle = CreateFile(path, delete, 0, IntPtr.Zero, createNew, deleteOnClose | noFollow, IntPtr.Zero);
        if (!handle.IsInvalid) return handle;
        var error = Win32("Cannot reserve the vacant original build path " + path);
        handle.Dispose();
        throw error;
    }

    private static List<(string Name, bool Directory)> ReadChildren(SafeFileHandle directory)
    {
        const int bytes = 16 * 1024;
        var children = new List<(string, bool)>();
        IntPtr buffer = Marshal.AllocHGlobal(bytes);
        try
        {
            int information = 11; // FileIdBothDirectoryRestartInfo
            while (GetFileInformationByHandleEx(directory, information, buffer, bytes))
            {
                information = 10; // FileIdBothDirectoryInfo
                for (int offset = 0;;)
                {
                    const int fileName = 104;
                    if (offset > bytes - fileName) throw new IOException("Invalid Windows directory entry.");
                    int next = Marshal.ReadInt32(buffer, offset);
                    int attributes = Marshal.ReadInt32(buffer, offset + 56);
                    int length = Marshal.ReadInt32(buffer, offset + 60);
                    if (length <= 0 || (length & 1) != 0 || length > bytes - offset - fileName)
                        throw new IOException("Invalid Windows directory entry name.");
                    string name = Marshal.PtrToStringUni(buffer + offset + fileName, length / 2)!;
                    if (name is not ("." or ".."))
                    {
                        if (name.IndexOfAny(['/', '\\', ':', '\0']) >= 0 || ((FileAttributes)attributes).HasFlag(FileAttributes.ReparsePoint))
                            throw new IOException("Unsupported entry in the isolated build.");
                        children.Add((name, ((FileAttributes)attributes).HasFlag(FileAttributes.Directory)));
                        if (children.Count > 100_000) throw new IOException("Build folder exceeds the inspection limit.");
                    }
                    if (next == 0) break;
                    if (next < fileName + length || next > bytes - offset - fileName)
                        throw new IOException("Invalid Windows directory entry offset.");
                    offset += next;
                }
            }
            if (Marshal.GetLastWin32Error() != 18) throw Win32("Cannot enumerate the verified build directory");
            return children;
        }
        finally { Marshal.FreeHGlobal(buffer); }
    }

    private static SafeFileHandle OpenChild(SafeFileHandle parent, string name, bool directory)
    {
        IntPtr text = Marshal.StringToHGlobalUni(name);
        IntPtr nativeName = Marshal.AllocHGlobal(Marshal.SizeOf<UnicodeString>());
        try
        {
            var length = checked((ushort)(name.Length * sizeof(char)));
            Marshal.StructureToPtr(new UnicodeString { Length = length, MaximumLength = length, Buffer = text }, nativeName, false);
            var attributes = new ObjectAttributes { Length = Marshal.SizeOf<ObjectAttributes>(), Root = parent.DangerousGetHandle(), Name = nativeName };
            uint access = directory ? 0x02100000u : 0x00110081u;
            uint options = 0x00200020u | (directory ? 1u : 0x40u);
            int status = NtCreateFile(out var handle, access, ref attributes, out _, IntPtr.Zero, 0, 0, 1, options, IntPtr.Zero, 0);
            GC.KeepAlive(parent);
            if (status == 0 && !handle.IsInvalid) return handle;
            handle.Dispose();
            throw new Win32Exception((int)RtlNtStatusToDosError(status), "Cannot exclusively open isolated entry " + name);
        }
        finally { Marshal.FreeHGlobal(nativeName); Marshal.FreeHGlobal(text); }
    }

    private static byte[] ReadSecurity(SafeFileHandle handle)
    {
        uint error = GetSecurityInfo(handle, 1, 7, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero, out IntPtr descriptor);
        if (error != 0) throw new Win32Exception((int)error, "Cannot read build directory permissions.");
        try
        {
            int size = GetSecurityDescriptorLength(descriptor);
            if (size is <= 0 or > 65536) throw new IOException("Unsupported build permission descriptor length.");
            var result = new byte[size];
            Marshal.Copy(descriptor, result, 0, size);
            return result;
        }
        finally { _ = LocalFree(descriptor); }
    }

    private static byte[] SealedDescriptor(byte[] original)
    {
        var security = new RawSecurityDescriptor(original, 0);
        const ControlFlags supported = ControlFlags.SelfRelative | ControlFlags.DiscretionaryAclPresent |
            ControlFlags.DiscretionaryAclAutoInherited;
        if (security.InvalidForCleanup(supported)) throw new IOException("Build directory permissions cannot be restored exactly.");
        security.DiscretionaryAcl!.InsertAce(0, new CommonAce(AceFlags.None, AceQualifier.AccessDenied, 6,
            new SecurityIdentifier(WellKnownSidType.WorldSid, null), false, null));
        var result = new byte[security.BinaryLength];
        security.GetBinaryForm(result, 0);
        return result;
    }

    private static bool InvalidForCleanup(this RawSecurityDescriptor security, ControlFlags supported) =>
        (security.ControlFlags & ~supported) != 0 || security.DiscretionaryAcl is null || security.Owner is null || security.Group is null;

    private static void WriteSecurity(SafeFileHandle handle, byte[] descriptor)
    {
        var security = new RawSecurityDescriptor(descriptor, 0);
        if (security.ControlFlags.HasFlag(ControlFlags.DiscretionaryAclAutoInherited))
        {
            var acl = security.DiscretionaryAcl!;
            var bytes = new byte[acl.BinaryLength];
            acl.GetBinaryForm(bytes, 0);
            uint error = SetSecurityInfo(handle, 1, 4, IntPtr.Zero, IntPtr.Zero, bytes, IntPtr.Zero);
            if (error != 0) throw new Win32Exception((int)error, "Cannot set build directory permissions.");
            return;
        }
        // SetSecurityInfo adds the auto-inherited control bit even when it did
        // not exist. Use a DACL-only native update for that descriptor form.
        int status = NtSetSecurityObject(handle, 4, descriptor);
        if (status != 0) throw new Win32Exception((int)RtlNtStatusToDosError(status), "Cannot set build directory permissions.");
    }

    private static SafeFileHandle Open(string path, bool directory, bool deleting)
    {
        const uint readDataAndAttributes = 0x81, delete = 0x10000;
        const uint openReparsePoint = 0x00200000, backupSemantics = 0x02000000;
        uint share = deleting ? (directory ? 1u : 0u) : 3u;
        var handle = CreateFile(path, readDataAndAttributes | (deleting ? delete : 0), share, IntPtr.Zero, 3,
            openReparsePoint | backupSemantics, IntPtr.Zero);
        if (!handle.IsInvalid) return handle;
        var error = Win32("Cannot guard " + path);
        handle.Dispose();
        throw error;
    }

    private static Entry Describe(SafeFileHandle handle, string relative, bool directory)
    {
        if (!GetFileInformationByHandle(handle, out var info)) throw Win32("Cannot read build file identity");
        var attributes = (FileAttributes)info.Attributes;
        if (attributes.HasFlag(FileAttributes.ReparsePoint) || attributes.HasFlag(FileAttributes.Directory) != directory ||
            (!directory && (info.Links != 1 || GetFileType(handle) != 1)))
            throw new IOException("Build folder contains an unsupported entry: " + relative);
        return new(relative, directory, Identity(info),
            directory ? 0 : checked((long)Join(info.SizeHigh, info.SizeLow)), (long)Join(info.WrittenHigh, info.WrittenLow));
    }

    private static FileIdentity Identity(FileInformation info) => new(info.Volume, Join(info.IndexHigh, info.IndexLow));
    private static ulong Join(uint high, uint low) => ((ulong)high << 32) | low;

    private static readonly HashSet<string> RelevantProcesses = new(StringComparer.OrdinalIgnoreCase)
    {
        "Atlantis.Editor", "Atlantis.Editor.Tests", "atlantis_runtime", "cmake", "MSBuild", "ninja",
        "cl", "link", "mspdbsrv", "csc", "rc", "mt", "dxc", "dotnet", "devenv", "ctest",
    };

    internal static void VerifyProcessUse(string root, FileIdentity attempt)
    {
        string leaf = Path.GetFileName(root);
        var shortPath = new StringBuilder(32768);
        if (GetShortPathName(root, shortPath, (uint)shortPath.Capacity) == 0)
            throw Win32("Cannot verify alternate names for the build folder");
        string shortLeaf = Path.GetFileName(shortPath.ToString());
        bool References(string pathOrCommand) => pathOrCommand.Contains(leaf, StringComparison.OrdinalIgnoreCase) ||
            pathOrCommand.Contains(shortLeaf, StringComparison.OrdinalIgnoreCase);
        var processes = Process.GetProcesses();
        try
        {
            foreach (var process in processes)
            {
                string processName = process.ProcessName;
                if (!RelevantProcesses.Contains(processName)) continue;
                using var handle = OpenProcess(0x1000, false, process.Id);
                if (handle.IsInvalid)
                {
                    if (Marshal.GetLastWin32Error() == 87) continue; // exited
                    throw Win32("Cannot verify process use of the build folder");
                }
                var image = new StringBuilder(32768);
                uint imageLength = (uint)image.Capacity;
                if (!QueryFullProcessImageName(handle, 0, image, ref imageLength))
                    throw Win32("Cannot verify the executable of a relevant Windows process");
                string command = ReadCommandLine(handle);
                if (References(image.ToString()) || References(command))
                    throw new IOException("A process is using this build folder.");
                if (processName.Equals("Atlantis.Editor", StringComparison.OrdinalIgnoreCase) ||
                    processName.Equals("dotnet", StringComparison.OrdinalIgnoreCase))
                    VerifyEditorRuntime(command, processName, attempt);
            }
        }
        finally
        {
            foreach (var process in processes) process.Dispose();
        }
    }

    private static void VerifyEditorRuntime(string command, string processName, FileIdentity attempt)
    {
        IntPtr arguments = CommandLineToArgv(command, out int count);
        if (arguments == IntPtr.Zero) throw Win32("Cannot read the startup arguments of a running Editor");
        try
        {
            string Arg(int index) => Marshal.PtrToStringUni(Marshal.ReadIntPtr(arguments, index * IntPtr.Size))!;
            bool nativeAppHost = processName.Equals("Atlantis.Editor", StringComparison.OrdinalIgnoreCase);
            int editorStart = 1;
            if (!nativeAppHost)
            {
                if (editorStart < count && Arg(editorStart) == "exec")
                {
                    ++editorStart;
                    while (editorStart + 1 < count && Arg(editorStart) is "--depsfile" or "--runtimeconfig" or
                               "--additionalprobingpath" or "--additional-deps" or "--fx-version" or "--roll-forward")
                        editorStart += 2;
                }
                if (editorStart >= count || !Path.GetFileName(Arg(editorStart)).Equals("Atlantis.Editor.dll", StringComparison.OrdinalIgnoreCase))
                    return;
                ++editorStart;
            }
            string? runtime = null;
            for (int index = editorStart; index + 1 < count; ++index)
                if (Arg(index) == "--runtime") runtime = Arg(++index);
            if (runtime is null || !Path.IsPathFullyQualified(runtime))
                throw new IOException("Cannot verify which build a running Editor is using.");
            VerifyRuntimeTarget(runtime, attempt);
        }
        finally { _ = LocalFree(arguments); }
    }

    private static void VerifyRuntimeTarget(string runtime, FileIdentity attempt)
    {
        // Follow aliases only for this read-only observation. Deletion still
        // opens each managed directory and file without following reparse points.
        const uint readAttributes = 0x80, shareAll = 7, openExisting = 3, backupSemantics = 0x02000000;
        using var target = CreateFile(runtime, readAttributes, shareAll, IntPtr.Zero, openExisting, backupSemantics, IntPtr.Zero);
        if (target.IsInvalid) throw Win32("Cannot resolve the Runtime of a running Editor");
        if (!GetFileInformationByHandle(target, out var targetInfo)) throw Win32("Cannot identify the Runtime of a running Editor");
        if (((FileAttributes)targetInfo.Attributes).HasFlag(FileAttributes.Directory) || GetFileType(target) != 1)
            throw new IOException("Cannot verify the Runtime file of a running Editor.");

        string FinalTarget()
        {
            var resolved = new StringBuilder(32768);
            uint length = GetFinalPathNameByHandle(target, resolved, (uint)resolved.Capacity, 0);
            if (length == 0) throw Win32("Cannot resolve the Runtime path of a running Editor");
            if (length >= resolved.Capacity) throw new IOException("The resolved Runtime path is too long.");
            return resolved.ToString();
        }

        string path = FinalTarget();
        for (string? parent = Path.GetDirectoryName(path); parent is not null; parent = Path.GetDirectoryName(parent))
        {
            using var ancestor = CreateFile(parent, readAttributes, shareAll, IntPtr.Zero, openExisting, backupSemantics, IntPtr.Zero);
            if (ancestor.IsInvalid || !GetFileInformationByHandle(ancestor, out var info))
                throw Win32("Cannot identify a directory in the resolved Runtime path");
            if (Identity(info) == attempt)
                throw new IOException("An Editor is using this build folder through its Runtime path.");
        }
        if (FinalTarget() != path) throw new IOException("The Runtime path changed during inspection.");
    }

    private static string ReadCommandLine(SafeFileHandle process)
    {
        const int bytes = 65536 + 32;
        IntPtr buffer = Marshal.AllocHGlobal(bytes);
        try
        {
            int status = NtQueryInformationProcess(process, 60, buffer, bytes, out _);
            if (status != 0) throw new IOException("Cannot verify the command line of a relevant Windows process.");
            int length = (ushort)Marshal.ReadInt16(buffer);
            if (length == 0 || length % 2 != 0)
                throw new IOException("Cannot verify the command line of a relevant Windows process.");
            IntPtr chars = Marshal.ReadIntPtr(buffer, IntPtr.Size);
            long offset = chars.ToInt64() - buffer.ToInt64();
            if (offset < 0 || offset > bytes - length)
                throw new IOException("Unexpected Windows process command-line information.");
            return Marshal.PtrToStringUni(chars, length / 2)!;
        }
        finally { Marshal.FreeHGlobal(buffer); }
    }

    internal sealed class BuildJob : IDisposable
    {
        private readonly SafeFileHandle _job = CreateJobObject(IntPtr.Zero, null);
        private bool _certain = true;

        internal bool Available => !_job.IsInvalid;

        internal bool Settled() => _certain && Available &&
            QueryInformationJobObject(_job, 1, out var accounting, Marshal.SizeOf<JobAccounting>(), IntPtr.Zero) &&
            accounting.ActiveProcesses == 0;

        internal async Task<bool> ObserveIdleAsync()
        {
            if (!_certain || !Available) return false;
            var delay = Stopwatch.StartNew();
            while (!Settled())
            {
                if (delay.Elapsed >= TimeSpan.FromSeconds(1)) return false;
                await Task.Delay(50);
            }
            return true;
        }

        internal TrackedBuild? TryStart(ProcessStartInfo start)
        {
            if (!_certain || !Available) return null;
            try { return StartTracked(start, _job); }
            catch (Win32Exception) { _certain = false; return null; }
        }

        public void Dispose() => _job.Dispose();
    }

    internal sealed class TrackedBuild(SafeFileHandle process, StreamReader output, StreamReader error) : IDisposable
    {
        internal StreamReader Output { get; } = output;
        internal StreamReader Error { get; } = error;
        internal Task<int> ExitCodeAsync() => Task.Run(() =>
        {
            if (WaitForSingleObject(process, uint.MaxValue) != 0 || !GetExitCodeProcess(process, out uint code))
                throw Win32("Cannot settle CMake");
            return unchecked((int)code);
        });

        public void Dispose()
        {
            Output.Dispose();
            Error.Dispose();
            process.Dispose();
        }
    }

    private static TrackedBuild StartTracked(ProcessStartInfo start, SafeFileHandle job)
    {
        var security = new SecurityAttributes { Size = Marshal.SizeOf<SecurityAttributes>(), Inherit = 1 };
        using var input = CreateFile("NUL", 0x80000000, 3, ref security, 3, 0, IntPtr.Zero);
        if (input.IsInvalid) throw Win32("Cannot open CMake input");
        SafeFileHandle? output = null, error = null;
        try
        {
            using var stdout = OpenPipe(ref security, out output);
            using var stderr = OpenPipe(ref security, out error);
            using var attributes = new ProcessAttributes();
            attributes.Set(0x00020002, input, stdout, stderr);
            attributes.Set(0x0002000d, job);
            var info = new StartupInfoEx
            {
                Startup = new() { Size = Marshal.SizeOf<StartupInfoEx>(), Flags = 0x100,
                    Input = input.DangerousGetHandle(), Output = stdout.DangerousGetHandle(), Error = stderr.DangerousGetHandle() },
                Attributes = attributes.Pointer,
            };
            var command = new StringBuilder(string.Join(' ', new[] { start.FileName }.Concat(start.ArgumentList).Select(Quote)));
            // Prepare readers before invoking the tool so fallback is possible
            // only before a tracked invocation succeeds.
            var outputReader = new StreamReader(new FileStream(output, FileAccess.Read), Console.OutputEncoding);
            var errorReader = new StreamReader(new FileStream(error, FileAccess.Read), Console.OutputEncoding);
            // A reused MSBuild worker can outlive this build or already belong
            // to another job. Scope this setting to the tracked invocation.
            var variables = new SortedDictionary<string, string?>(start.Environment, StringComparer.OrdinalIgnoreCase)
                { ["MSBUILDDISABLENODEREUSE"] = "1" };
            string environmentBlock = string.Join('\0', variables.Select(variable => $"{variable.Key}={variable.Value}")) + "\0\0";
            IntPtr environment = Marshal.StringToHGlobalUni(environmentBlock);
            ProcessInformation child;
            try
            {
                const uint unicodeEnvironment = 0x400, extendedStartupInfo = 0x80000, noWindow = 0x08000000;
                if (!CreateProcess(start.FileName, command, IntPtr.Zero, IntPtr.Zero, true,
                                   noWindow | extendedStartupInfo | unicodeEnvironment, environment,
                                   start.WorkingDirectory, ref info, out child))
                    throw Win32("Cannot start CMake in its build job");
            }
            finally { Marshal.FreeHGlobal(environment); }
            using var thread = new SafeFileHandle(child.Thread, ownsHandle: true);
            output = error = null;  // the returned readers own the read handles
            return new(new SafeFileHandle(child.Process, ownsHandle: true), outputReader, errorReader);
        }
        finally { output?.Dispose(); error?.Dispose(); }
    }

    private static SafeFileHandle OpenPipe(ref SecurityAttributes security, out SafeFileHandle read)
    {
        if (!CreatePipe(out read, out var write, ref security, 0)) throw Win32("Cannot redirect CMake output");
        if (SetHandleInformation(read, 1, 0)) return write;
        var error = Win32("Cannot protect CMake pipe ownership");
        read.Dispose();
        write.Dispose();
        throw error;
    }

    private static string Quote(string argument)
    {
        var quoted = new StringBuilder("\"");
        int slashes = 0;
        foreach (char c in argument)
        {
            if (c == '\\') { ++slashes; continue; }
            quoted.Append('\\', c == '"' ? slashes * 2 + 1 : slashes);
            quoted.Append(c);
            slashes = 0;
        }
        return quoted.Append('\\', slashes * 2).Append('"').ToString();
    }

    private sealed class ProcessAttributes : IDisposable
    {
        private readonly List<IntPtr> _values = [];
        public IntPtr Pointer { get; }

        internal ProcessAttributes()
        {
            nuint size = 0;
            _ = InitializeProcThreadAttributeList(IntPtr.Zero, 2, 0, ref size);
            Pointer = Marshal.AllocHGlobal(checked((int)size));
            if (InitializeProcThreadAttributeList(Pointer, 2, 0, ref size)) return;
            var error = Win32("Cannot create CMake process attributes");
            Marshal.FreeHGlobal(Pointer);
            throw error;
        }

        internal void Set(nuint attribute, params SafeFileHandle[] handles)
        {
            int size = handles.Length * IntPtr.Size;
            IntPtr values = Marshal.AllocHGlobal(size);
            _values.Add(values);
            for (int index = 0; index < handles.Length; ++index)
                Marshal.WriteIntPtr(values, index * IntPtr.Size, handles[index].DangerousGetHandle());
            if (!UpdateProcThreadAttribute(Pointer, 0, attribute, values, (nuint)size, IntPtr.Zero, IntPtr.Zero))
                throw Win32("Cannot configure CMake process attributes");
        }

        public void Dispose()
        {
            DeleteProcThreadAttributeList(Pointer);
            foreach (IntPtr value in _values) Marshal.FreeHGlobal(value);
            Marshal.FreeHGlobal(Pointer);
        }
    }

    private static Win32Exception Win32(string operation)
    {
        int error = Marshal.GetLastWin32Error();
        return new(error, operation + ": " + new Win32Exception(error).Message);
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct FileInformation
    {
        internal uint Attributes, CreatedLow, CreatedHigh, AccessedLow, AccessedHigh, WrittenLow, WrittenHigh;
        internal uint Volume, SizeHigh, SizeLow, Links, IndexHigh, IndexLow;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct JobAccounting
    {
        internal long UserTime, KernelTime, PeriodUserTime, PeriodKernelTime;
        internal uint Faults, TotalProcesses, ActiveProcesses, TerminatedProcesses;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct SecurityAttributes { internal int Size; internal IntPtr Descriptor; internal int Inherit; }

    [StructLayout(LayoutKind.Sequential)]
    private struct StartupInfo
    {
        internal int Size;
        internal IntPtr Reserved, Desktop, Title;
        internal uint X, Y, Width, Height, XChars, YChars, Fill, Flags;
        internal ushort Show, ReservedSize;
        internal IntPtr ReservedBytes, Input, Output, Error;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct StartupInfoEx { internal StartupInfo Startup; internal IntPtr Attributes; }

    [StructLayout(LayoutKind.Sequential)]
    private struct ProcessInformation { internal IntPtr Process, Thread; internal uint Pid, Tid; }

    [StructLayout(LayoutKind.Sequential)]
    private struct UnicodeString { internal ushort Length, MaximumLength; internal IntPtr Buffer; }

    [StructLayout(LayoutKind.Sequential)]
    private struct ObjectAttributes
    {
        internal int Length;
        internal IntPtr Root, Name;
        internal uint Attributes;
        internal IntPtr Security, Impersonation;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct IoStatusBlock { internal IntPtr Status, Information; }

    [DllImport("kernel32.dll", EntryPoint = "CreateFileW", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern SafeFileHandle CreateFile(string path, uint access, uint share, IntPtr security, uint mode, uint flags, IntPtr template);
    [DllImport("kernel32.dll", EntryPoint = "CreateFileW", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern SafeFileHandle CreateFile(string path, uint access, uint share, ref SecurityAttributes security, uint mode, uint flags, IntPtr template);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool GetFileInformationByHandle(SafeFileHandle file, out FileInformation information);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern uint GetFileType(SafeFileHandle file);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool SetFileInformationByHandle(SafeFileHandle file, int informationClass, ref byte information, int size);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool SetFileInformationByHandle(SafeFileHandle file, int informationClass, byte[] information, int size);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool GetFileInformationByHandleEx(SafeFileHandle file, int informationClass, IntPtr information, int size);
    [DllImport("ntdll.dll")]
    private static extern int NtCreateFile(out SafeFileHandle file, uint access, ref ObjectAttributes attributes, out IoStatusBlock io,
        IntPtr size, uint fileAttributes, uint share, uint disposition, uint options, IntPtr extendedAttributes, uint extendedSize);
    [DllImport("ntdll.dll")]
    private static extern uint RtlNtStatusToDosError(int status);
    [DllImport("ntdll.dll")]
    private static extern int NtSetSecurityObject(SafeFileHandle file, uint flags, byte[] security);
    [DllImport("advapi32.dll")]
    private static extern uint GetSecurityInfo(SafeFileHandle file, uint kind, uint flags, IntPtr owner, IntPtr group,
        IntPtr dacl, IntPtr sacl, out IntPtr descriptor);
    [DllImport("advapi32.dll")]
    private static extern int GetSecurityDescriptorLength(IntPtr descriptor);
    [DllImport("advapi32.dll")]
    private static extern uint SetSecurityInfo(SafeFileHandle file, uint kind, uint flags, IntPtr owner, IntPtr group, byte[] dacl, IntPtr sacl);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern SafeFileHandle OpenProcess(uint access, bool inherit, int processId);
    [DllImport("kernel32.dll", EntryPoint = "GetShortPathNameW", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern uint GetShortPathName(string path, StringBuilder shortPath, uint length);
    [DllImport("kernel32.dll", EntryPoint = "GetFinalPathNameByHandleW", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern uint GetFinalPathNameByHandle(SafeFileHandle file, StringBuilder path, uint length, uint flags);
    [DllImport("kernel32.dll", EntryPoint = "QueryFullProcessImageNameW", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern bool QueryFullProcessImageName(SafeFileHandle process, uint flags, StringBuilder image, ref uint length);
    [DllImport("shell32.dll", EntryPoint = "CommandLineToArgvW", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern IntPtr CommandLineToArgv(string command, out int count);
    [DllImport("kernel32.dll")]
    private static extern IntPtr LocalFree(IntPtr memory);
    [DllImport("ntdll.dll")]
    private static extern int NtQueryInformationProcess(SafeFileHandle process, int informationClass, IntPtr information, int size, out int written);
    [DllImport("kernel32.dll", EntryPoint = "CreateJobObjectW", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern SafeFileHandle CreateJobObject(IntPtr security, string? name);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool QueryInformationJobObject(SafeFileHandle job, int informationClass, out JobAccounting information, int size, IntPtr written);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool CreatePipe(out SafeFileHandle read, out SafeFileHandle write, ref SecurityAttributes security, uint size);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool SetHandleInformation(SafeFileHandle handle, uint mask, uint flags);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool InitializeProcThreadAttributeList(IntPtr list, int count, int flags, ref nuint size);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool UpdateProcThreadAttribute(IntPtr list, uint flags, nuint attribute, IntPtr value, nuint size, IntPtr previous, IntPtr written);
    [DllImport("kernel32.dll")]
    private static extern void DeleteProcThreadAttributeList(IntPtr list);
    [DllImport("kernel32.dll", EntryPoint = "CreateProcessW", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern bool CreateProcess(string application, StringBuilder command, IntPtr processAttributes, IntPtr threadAttributes,
        bool inheritHandles, uint flags, IntPtr environment, string directory, ref StartupInfoEx startup, out ProcessInformation process);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern uint WaitForSingleObject(SafeFileHandle handle, uint milliseconds);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool GetExitCodeProcess(SafeFileHandle process, out uint code);
}
