function ConvertTo-CodeViewPdbIdentity {
  param([Parameter(Mandatory)] [string] $Identity)

  $match = [regex]::Match($Identity, '^(?<guid>[0-9A-Fa-f]{32}),(?<age>\d+)$')
  if (-not $match.Success) { throw "Invalid PDB identity '$Identity'." }
  $guid = $match.Groups['guid'].Value.ToUpperInvariant()
  # llvm-pdbutil prints the first three GUID fields in byte-stream order, while
  # CodeView and crash reports render those RSDS bytes as a Windows GUID.
  $codeViewGuid = $guid.Substring(6, 2) + $guid.Substring(4, 2) + $guid.Substring(2, 2) +
                  $guid.Substring(0, 2) + $guid.Substring(10, 2) + $guid.Substring(8, 2) +
                  $guid.Substring(14, 2) + $guid.Substring(12, 2) + $guid.Substring(16, 16)
  "$codeViewGuid,$($match.Groups['age'].Value)"
}

function Test-PdbIdentityMatch {
  param(
    [Parameter(Mandatory)] [string] $ExecutableIdentity,
    [Parameter(Mandatory)] [string] $PdbIdentity
  )

  if ($PdbIdentity -ceq $ExecutableIdentity) { return $true }
  (ConvertTo-CodeViewPdbIdentity -Identity $PdbIdentity) -ceq $ExecutableIdentity
}
