param(
  [int]$MinimumWidth = 1600,
  [int]$MinimumHeight = 1000,
  [switch]$CheckOnly
)

$ErrorActionPreference = 'Stop'

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
public struct QtTestDevMode {
  [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string dmDeviceName;
  public short dmSpecVersion;
  public short dmDriverVersion;
  public short dmSize;
  public short dmDriverExtra;
  public uint dmFields;
  public int dmPositionX;
  public int dmPositionY;
  public uint dmDisplayOrientation;
  public uint dmDisplayFixedOutput;
  public short dmColor;
  public short dmDuplex;
  public short dmYResolution;
  public short dmTTOption;
  public short dmCollate;
  [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string dmFormName;
  public short dmLogPixels;
  public uint dmBitsPerPel;
  public uint dmPelsWidth;
  public uint dmPelsHeight;
  public uint dmDisplayFlags;
  public uint dmDisplayFrequency;
  public uint dmICMMethod;
  public uint dmICMIntent;
  public uint dmMediaType;
  public uint dmDitherType;
  public uint dmReserved1;
  public uint dmReserved2;
  public uint dmPanningWidth;
  public uint dmPanningHeight;
}

public static class QtTestDisplay {
  public const int ENUM_CURRENT_SETTINGS = -1;
  public const int DISP_CHANGE_SUCCESSFUL = 0;
  public const uint CDS_TEST = 2;
  public const uint DM_PELSWIDTH = 0x00080000;
  public const uint DM_PELSHEIGHT = 0x00100000;

  [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "EnumDisplaySettingsW")]
  public static extern bool EnumDisplaySettings(string device, int mode, ref QtTestDevMode settings);

  [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "ChangeDisplaySettingsExW")]
  public static extern int ChangeDisplaySettingsEx(string device, ref QtTestDevMode settings,
      IntPtr window, uint flags, IntPtr parameter);
}
"@

$displayDevice = '\\.\DISPLAY1'

function New-DisplayMode {
  $mode = New-Object QtTestDevMode
  $mode.dmDeviceName = [string]::new([char]0, 32)
  $mode.dmFormName = [string]::new([char]0, 32)
  $mode.dmSize = [short][Runtime.InteropServices.Marshal]::SizeOf([type][QtTestDevMode])
  return $mode
}

$current = New-DisplayMode
if (-not [QtTestDisplay]::EnumDisplaySettings($displayDevice, [QtTestDisplay]::ENUM_CURRENT_SETTINGS, [ref]$current)) {
  throw "Could not read the current Windows display mode for $displayDevice."
}

if ($current.dmPelsWidth -ge $MinimumWidth -and $current.dmPelsHeight -ge $MinimumHeight) {
  Write-Host "Qt test display is already $($current.dmPelsWidth)x$($current.dmPelsHeight)."
  exit 0
}

$modes = [System.Collections.Generic.List[QtTestDevMode]]::new()
for ($index = 0; ; $index++) {
  $mode = New-DisplayMode
  if (-not [QtTestDisplay]::EnumDisplaySettings($displayDevice, $index, [ref]$mode)) { break }
  if ($mode.dmBitsPerPel -lt 32 -or $mode.dmPelsWidth -lt $MinimumWidth -or $mode.dmPelsHeight -lt $MinimumHeight) {
    continue
  }
  $mode.dmFields = $mode.dmFields -bor [QtTestDisplay]::DM_PELSWIDTH -bor [QtTestDisplay]::DM_PELSHEIGHT
  if ([QtTestDisplay]::ChangeDisplaySettingsEx($displayDevice, [ref]$mode, [IntPtr]::Zero,
      [QtTestDisplay]::CDS_TEST, [IntPtr]::Zero) -eq [QtTestDisplay]::DISP_CHANGE_SUCCESSFUL) {
    $modes.Add($mode)
  }
}

$selected = $modes | Sort-Object { [long]$_.dmPelsWidth * [long]$_.dmPelsHeight }, dmPelsWidth, dmPelsHeight | Select-Object -First 1
if (-not $selected) {
  throw "No supported 32-bit display mode is at least ${MinimumWidth}x${MinimumHeight}; current mode is $($current.dmPelsWidth)x$($current.dmPelsHeight)."
}

if ($CheckOnly) {
  Write-Host "A supported Qt test mode is available: $($selected.dmPelsWidth)x$($selected.dmPelsHeight). Current mode remains unchanged."
  exit 0
}

$result = [QtTestDisplay]::ChangeDisplaySettingsEx($displayDevice, [ref]$selected, [IntPtr]::Zero, 0, [IntPtr]::Zero)
if ($result -ne [QtTestDisplay]::DISP_CHANGE_SUCCESSFUL) {
  throw "Could not select the Qt test display mode ($result)."
}

$updated = New-DisplayMode
if (-not [QtTestDisplay]::EnumDisplaySettings($displayDevice, [QtTestDisplay]::ENUM_CURRENT_SETTINGS, [ref]$updated) -or
    $updated.dmPelsWidth -lt $MinimumWidth -or $updated.dmPelsHeight -lt $MinimumHeight) {
  throw 'Windows did not apply the selected Qt test display mode.'
}
Write-Host "Selected Qt test display: $($updated.dmPelsWidth)x$($updated.dmPelsHeight)."
