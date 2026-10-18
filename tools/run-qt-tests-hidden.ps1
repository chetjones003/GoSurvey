<#
.SYNOPSIS
  Runs the Qt GUI test executable on a private, invisible Windows desktop.

.DESCRIPTION
  GoSurveyQtTests opens real top-level windows. Run normally they flash on the user's screen,
  steal focus, and a test that hangs leaves a window up until someone closes it. This starts the
  executable on its own desktop (CreateDesktop), which is never displayed and has its own input
  queue, so nothing appears and no keystrokes or clicks are shared with the user's session. The
  real GPU driver is still used, so the OpenGL viewport tests run (Qt's "offscreen" platform has
  no OpenGL on Windows).

  The process is killed if it exceeds -TimeoutSec, so a hung test cannot stall the caller.
  The exit code is the test executable's own (0 = all passed; 124 = timed out).

.EXAMPLE
  powershell -File tools/run-qt-tests-hidden.ps1 -Exe build/GoSurveyQtTests.exe
  powershell -File tools/run-qt-tests-hidden.ps1 -Exe build/GoSurveyQtTests.exe updateOfferShowsBothVersionsAndWaitsForAnExplicitChoice
#>
param(
  [Parameter(Mandatory = $true)][string]$Exe,
  [int]$TimeoutSec = 600,
  [string]$Log = "",
  [string]$WorkingDirectory = "",
  # Anything left over is passed to the test executable, e.g. test function names.
  [Parameter(ValueFromRemainingArguments = $true)][string[]]$TestArgs = @()
)

$ErrorActionPreference = 'Stop'
$exePath = (Resolve-Path -LiteralPath $Exe).Path
$workDir = $WorkingDirectory
if (-not $workDir) { $workDir = Join-Path (Split-Path -Parent $exePath) 'artifacts/qt' }
New-Item -ItemType Directory -Force -Path $workDir | Out-Null
$workDir = (Resolve-Path -LiteralPath $workDir).Path
if (-not $Log) { $Log = Join-Path $workDir "qt-tests-$PID.log" }
$Log = [IO.Path]::GetFullPath($Log)
if (Test-Path -LiteralPath $Log) { Remove-Item -LiteralPath $Log -Force }

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class HiddenDesktop {
  [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
  public struct STARTUPINFO {
    public int cb; public string lpReserved; public string lpDesktop; public string lpTitle;
    public int dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
    public short wShowWindow, cbReserved2; public IntPtr lpReserved2, hStdInput, hStdOutput, hStdError;
  }
  [StructLayout(LayoutKind.Sequential)]
  public struct PROCESS_INFORMATION { public IntPtr hProcess, hThread; public int dwProcessId, dwThreadId; }
  [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
  public static extern IntPtr CreateDesktopW(string name, IntPtr device, IntPtr devMode, int flags, uint access, IntPtr sa);
  [DllImport("user32.dll", SetLastError = true)] public static extern bool CloseDesktop(IntPtr desktop);
  [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
  public static extern bool CreateProcessW(string app, string cmd, IntPtr pa, IntPtr ta, bool inherit, uint flags,
      IntPtr env, string cwd, ref STARTUPINFO si, out PROCESS_INFORMATION pi);
  [DllImport("kernel32.dll")] public static extern uint WaitForSingleObject(IntPtr h, uint ms);
  [DllImport("kernel32.dll")] public static extern bool GetExitCodeProcess(IntPtr h, out uint code);
  [DllImport("kernel32.dll")] public static extern bool TerminateProcess(IntPtr h, uint code);
  [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
}
"@

$GENERIC_ALL = 0x10000000
$desktopName = "gosurvey-qt-tests-$PID"
$desktop = [HiddenDesktop]::CreateDesktopW($desktopName, [IntPtr]::Zero, [IntPtr]::Zero, 0, $GENERIC_ALL, [IntPtr]::Zero)
if ($desktop -eq [IntPtr]::Zero) {
  throw "CreateDesktop failed (Win32 error $([Runtime.InteropServices.Marshal]::GetLastWin32Error()))"
}

try {
  $quoted = ($TestArgs | ForEach-Object { '"' + $_ + '"' }) -join ' '
  $cmd = "`"$exePath`" -o `"$Log,txt`" $quoted"
  $si = New-Object HiddenDesktop+STARTUPINFO
  $si.cb = [Runtime.InteropServices.Marshal]::SizeOf($si)
  $si.lpDesktop = "winsta0\$desktopName"
  $pi = New-Object HiddenDesktop+PROCESS_INFORMATION
  if (-not [HiddenDesktop]::CreateProcessW([NullString]::Value, $cmd, [IntPtr]::Zero, [IntPtr]::Zero, $false, 0,
                                           [IntPtr]::Zero, $workDir, [ref]$si, [ref]$pi)) {
    throw "CreateProcess failed (Win32 error $([Runtime.InteropServices.Marshal]::GetLastWin32Error()))"
  }
  try {
    $wait = [HiddenDesktop]::WaitForSingleObject($pi.hProcess, [uint32]($TimeoutSec * 1000))
    if ($wait -ne 0) {
      [void][HiddenDesktop]::TerminateProcess($pi.hProcess, 124)
      Write-Host "TIMED OUT after $TimeoutSec s; test process killed."
      $exitCode = 124
    } else {
      $code = [uint32]0
      [void][HiddenDesktop]::GetExitCodeProcess($pi.hProcess, [ref]$code)
      $exitCode = [int]$code
    }
  } finally {
    [void][HiddenDesktop]::CloseHandle($pi.hProcess)
    [void][HiddenDesktop]::CloseHandle($pi.hThread)
  }
} finally {
  [void][HiddenDesktop]::CloseDesktop($desktop)
}

if (Test-Path -LiteralPath $Log) {
  Get-Content -LiteralPath $Log | Select-String -Pattern 'FAIL!|Totals:|Actual|Expected|failure location|QFATAL' | ForEach-Object { $_.Line }
  Write-Host "Full log: $Log"
}
exit $exitCode
