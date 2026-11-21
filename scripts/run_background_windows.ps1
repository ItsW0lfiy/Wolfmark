[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet('test', 'all', 'package', 'setup-check')]
    [string]$Action,
    [switch]$AllowDirty
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$logDirectory = Join-Path $projectRoot 'out/tests/background'
New-Item -ItemType Directory -Force -Path $logDirectory | Out-Null
$runId = [Guid]::NewGuid().ToString('N')
$desktop = "WolfmarkTest-$runId"
$log = Join-Path $logDirectory "$Action-$runId.log"
$pwsh = (Get-Command pwsh -ErrorAction Stop).Source

Add-Type -TypeDefinition @"
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;

public static class WolfmarkTestDesktop {
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct StartupInfo {
        public int Size;
        public string Reserved, Desktop, Title;
        public int X, Y, Width, Height, Columns, Rows, Fill, Flags;
        public ushort ShowWindow, ReservedSize;
        public IntPtr ReservedData, Input, Output, Error;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct ProcessInfo {
        public IntPtr Process, Thread;
        public uint ProcessId, ThreadId;
    }

    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr CreateDesktop(string name, IntPtr device, IntPtr mode,
        uint flags, uint access, IntPtr security);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool CloseDesktop(IntPtr desktop);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool CreateProcess(string application, StringBuilder arguments,
        IntPtr processSecurity, IntPtr threadSecurity, bool inheritHandles, uint flags,
        IntPtr environment, string directory, ref StartupInfo startup, out ProcessInfo process);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool GetExitCodeProcess(IntPtr process, out uint exitCode);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool CloseHandle(IntPtr handle);

    public static int Run(string name, string executable, string arguments, string directory) {
        const uint desktopAccess = 0x01ff;
        const uint createNoWindow = 0x08000000;
        IntPtr desktop = CreateDesktop(name, IntPtr.Zero, IntPtr.Zero, 0, desktopAccess, IntPtr.Zero);
        if (desktop == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
        try {
            var startup = new StartupInfo { Size = Marshal.SizeOf<StartupInfo>(), Desktop = name };
            ProcessInfo child;
            if (!CreateProcess(executable, new StringBuilder(arguments), IntPtr.Zero, IntPtr.Zero,
                false, createNoWindow, IntPtr.Zero, directory, ref startup, out child))
                throw new Win32Exception(Marshal.GetLastWin32Error());
            CloseHandle(child.Thread);
            try {
                uint code;
                if (WaitForSingleObject(child.Process, 0xffffffff) != 0 ||
                    !GetExitCodeProcess(child.Process, out code))
                    throw new Win32Exception(Marshal.GetLastWin32Error());
                return unchecked((int)code);
            } finally {
                CloseHandle(child.Process);
            }
        } finally {
            CloseDesktop(desktop);
        }
    }
}
"@

# The child inherits the working directory but has its own desktop. Failure to
# create that desktop stops the workflow without a foreground fallback.
$command = switch ($Action) {
    'test' { 'cargo test --workspace --locked --no-fail-fast -- --test-threads=1' }
    'all' { if ($AllowDirty) { '& ./scripts/all.ps1 -AllowDirty' } else { '& ./scripts/all.ps1' } }
    'package' { '& ./scripts/package_windows.ps1' }
    'setup-check' { '& ./scripts/test_windows_setup.ps1' }
}
$logLiteral = "'" + $log.Replace("'", "''") + "'"
$child = @"
`$ErrorActionPreference = 'Stop'
`$env:WOLFMARK_PRIVATE_TEST_DESKTOP = '1'
try {
    & { $command } *> $logLiteral
    exit `$LASTEXITCODE
} catch {
    Add-Content -LiteralPath $logLiteral -Value `$_.ToString()
    exit 1
}
"@
$encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($child))
Write-Host "Running $Action on a separate Windows desktop. Log: $log"
$result = [WolfmarkTestDesktop]::Run($desktop, $pwsh, "`"$pwsh`" -NoProfile -NonInteractive -EncodedCommand $encoded", $projectRoot)
if (Test-Path -LiteralPath $log) { Get-Content -LiteralPath $log }
exit $result
