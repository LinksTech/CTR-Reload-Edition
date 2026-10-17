# wait_exe_free.ps1 - waits, visibly and with an upper bound, until the linker
# can write an executable.
#
# Run by every executable target of CMakeLists.txt right before its link step
# (PRE_LINK, see ctr_wait_exe_free there), so build-msvc.bat, cmake --build and
# every other way of building get it alike.
#
# Why: an on-access virus scanner may keep an .exe open for a second or two
# after that exe was STARTED (it scans the image). A link that falls into that
# window fails with LNK1104 ("cannot open file"), although nothing is wrong
# with the build. This script does not retry the link and does not hide
# anything: it checks once whether the file can be opened the way the linker
# opens it (read + write, no sharing). If it can, or the file does not exist
# yet, it returns at once and prints nothing - this is the whole cost on a
# machine without such a scanner (the automatic builds), a few hundred
# milliseconds for starting PowerShell. If it cannot, it names the process
# that holds the file, re-checks about every 100 ms and lets the linker run as
# soon as the file is free. After the time limit (30 s) it gives up with a
# non-zero exit code, so the link step is not started and the build fails
# with the holder named. A build that only succeeds after a wait is a good
# build: the linker still writes on its first and only attempt.
#
# Log lines (fixed forms, each on a line of its own, prefix "exe-wait: "):
#
#   exe-wait: <file> locked by <process>(<pid>)[, <process>(<pid>)...], waiting...
#   exe-wait: <file> free after <ms> ms
#   exe-wait: <file> still locked by <process>(<pid>)[, ...] after <ms> ms, giving up (the linker is not started)
#   exe-wait: <file> not checked (Win32 error <n>), the linker reports it if it matters
#   exe-wait: <file> in use but up to date, nothing to link, not waiting
#
# (The last one only under the Visual Studio generators, see -Inputs below.)
#
# Every wait ends with exactly one "free after" line (or the "giving up" line,
# which fails the build). Count and sum the waits of a build log with:
#
#   grep -c "exe-wait: .* free after [0-9]* ms" build.log
#   grep -o "free after [0-9]* ms" build.log | awk '{s += $3} END {print s + 0}'
#
# The holder is found with NtQueryInformationFile(FileProcessIdsUsingFile),
# which needs no administrator rights. It is only compiled and asked when the
# file is locked, so the fast path stays a single open.
#
# Usage: powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass
#            -File wait_exe_free.ps1 <path of the exe> [<time limit in ms>]
#            [-Inputs <list of link inputs>]

param(
    [Parameter(Mandatory = $true)][string]$Path,
    [int]$TimeoutMs = 30000,
    [string]$Inputs = ''
)

# -Inputs <list file> (Visual Studio generators only): MSBuild runs a PRE_LINK
# step on EVERY build, also when it then finds the exe up to date and does not
# link at all. A running exe (the game or the Studio started from the build
# folder) must not make such a build wait and fail. So with -Inputs a locked
# exe is only waited for when a link is pending: when one of the link inputs is
# newer than the exe. The list file (written by CMake) names the target's
# object files (with the compiled resources, *.res) and the library files it
# links. Ninja and Makefile generators only run the step when they link, so
# they pass no list.
function Test-LinkPending([string]$exe, [string]$listFile) {
    if ($listFile -eq '' -or -not (Test-Path -LiteralPath $listFile)) { return $true }
    $stamp = (Get-Item -LiteralPath $exe).LastWriteTimeUtc
    foreach ($line in [System.IO.File]::ReadAllLines($listFile)) {
        $f = $line.Trim()
        if ($f -eq '') { continue }
        if (-not (Test-Path -LiteralPath $f)) { return $true }
        if ((Get-Item -LiteralPath $f).LastWriteTimeUtc -gt $stamp) { return $true }
    }
    return $false
}

# Returns 0 when the linker could open the file now (or it does not exist),
# else the Win32 error code of the attempt.
function Get-OpenError([string]$p) {
    try {
        $fs = [System.IO.File]::Open($p, [System.IO.FileMode]::Open,
            [System.IO.FileAccess]::ReadWrite, [System.IO.FileShare]::None)
        $fs.Dispose()
        return 0
    } catch {
        $e = $_.Exception
        while ($null -ne $e.InnerException) { $e = $e.InnerException }
        if ($e -is [System.IO.FileNotFoundException] -or
            $e -is [System.IO.DirectoryNotFoundException]) { return 0 }
        $code = $e.HResult -band 0xFFFF
        if ($code -eq 0) { $code = -1 }
        return $code
    }
}

# Only on the slow path: the processes that have the file open. Never fails:
# without an answer the holder is "unknown process".
function Get-Holders([string]$p) {
    try { return (Get-HoldersOrThrow $p) } catch { return 'unknown process' }
}

function Get-HoldersOrThrow([string]$p) {
    if (-not ('CtrExeWait.Holders' -as [type])) {
        # Inside a Visual Studio build, LIB names folders that need not exist;
        # the C# compiler behind Add-Type turns that into an error. It needs
        # no LIB, and this only changes this process's own environment.
        $env:LIB = $null
        Add-Type -ErrorAction Stop -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace CtrExeWait {
public static class Holders {
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern SafeFileHandle CreateFileW(string name, uint access, uint share,
        IntPtr sa, uint disposition, uint flags, IntPtr template);

    [StructLayout(LayoutKind.Sequential)]
    struct IO_STATUS_BLOCK { public IntPtr Status; public IntPtr Information; }

    [DllImport("ntdll.dll")]
    static extern int NtQueryInformationFile(SafeFileHandle h, out IO_STATUS_BLOCK iosb,
        IntPtr buffer, int length, int infoClass);

    const uint FILE_READ_ATTRIBUTES = 0x80;
    const uint FILE_SHARE_ALL = 7;
    const uint OPEN_EXISTING = 3;
    const int FileProcessIdsUsingFile = 47;

    // An attribute-only open takes no part in the sharing checks, so asking
    // cannot itself keep the linker out.
    public static List<int> Pids(string path) {
        var pids = new List<int>();
        using (var h = CreateFileW(path, FILE_READ_ATTRIBUTES, FILE_SHARE_ALL,
                   IntPtr.Zero, OPEN_EXISTING, 0, IntPtr.Zero)) {
            if (h.IsInvalid) return pids;
            int len = 64 * 1024;
            IntPtr buf = Marshal.AllocHGlobal(len);
            try {
                IO_STATUS_BLOCK iosb;
                if (NtQueryInformationFile(h, out iosb, buf, len, FileProcessIdsUsingFile) != 0)
                    return pids;
                // FILE_PROCESS_IDS_USING_FILE_INFORMATION: a ULONG count, then
                // ULONG_PTR process ids (pointer aligned).
                int count = Marshal.ReadInt32(buf);
                for (int i = 0; i < count; i++) {
                    long pid = IntPtr.Size == 8
                        ? Marshal.ReadInt64(buf, IntPtr.Size + i * IntPtr.Size)
                        : Marshal.ReadInt32(buf, IntPtr.Size + i * IntPtr.Size);
                    pids.Add((int)pid);
                }
            } finally { Marshal.FreeHGlobal(buf); }
        }
        pids.Sort();
        return pids;
    }
}
}
'@
    }
    $list = @()
    foreach ($procId in [CtrExeWait.Holders]::Pids($p)) {
        if ($procId -eq $PID) { continue }
        $name = '?'
        try { $name = (Get-Process -Id $procId -ErrorAction Stop).ProcessName } catch { }
        $list += ('{0}({1})' -f $name, $procId)
    }
    if ($list.Count -eq 0) { return 'unknown process' }
    return ($list -join ', ')
}

$file = Split-Path -Leaf $Path
$clock = [System.Diagnostics.Stopwatch]::StartNew()
$err = Get-OpenError $Path
if ($err -eq 0) { exit 0 }
if ($err -ne 32 -and $err -ne 33) {
    # Not a lock (32 = sharing violation, 33 = lock violation): nothing to wait for.
    Write-Output ('exe-wait: {0} not checked (Win32 error {1}), the linker reports it if it matters' -f $file, $err)
    exit 0
}
if (-not (Test-LinkPending $Path $Inputs)) {
    Write-Output ('exe-wait: {0} in use but up to date, nothing to link, not waiting' -f $file)
    exit 0
}

Write-Output ('exe-wait: {0} locked by {1}, waiting...' -f $file, (Get-Holders $Path))
while ($clock.ElapsedMilliseconds -lt $TimeoutMs) {
    Start-Sleep -Milliseconds 100
    $err = Get-OpenError $Path
    if ($err -eq 0 -or ($err -ne 32 -and $err -ne 33)) {
        # Free - or no longer a lock, which the linker then reports itself.
        Write-Output ('exe-wait: {0} free after {1} ms' -f $file, $clock.ElapsedMilliseconds)
        exit 0
    }
}
Write-Output ('exe-wait: {0} still locked by {1} after {2} ms, giving up (the linker is not started)' -f $file, (Get-Holders $Path), $clock.ElapsedMilliseconds)
exit 1
