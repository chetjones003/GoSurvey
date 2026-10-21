<#
.SYNOPSIS
  Validates a LandXML 1.2 file against the official schema (issue #1002).
.DESCRIPTION
  The schema is not vendored: download LandXML-1.2.xsd from
  http://www.landxml.org/schema/LandXML-1.2/LandXML-1.2.xsd and pass its path. Uses the .NET
  validating XML reader, an implementation independent of GoSurvey's own XML code.
.EXAMPLE
  powershell -NoProfile -File tools\validate-landxml.ps1 -File build\artifacts\site.xml -Schema LandXML-1.2.xsd
#>
param(
  [Parameter(Mandatory)][string]$File,
  [Parameter(Mandatory)][string]$Schema
)
$ErrorActionPreference = 'Stop'
$namespace = 'http://www.landxml.org/schema/LandXML-1.2'
$schemas = New-Object System.Xml.Schema.XmlSchemaSet
[void]$schemas.Add($namespace, (Resolve-Path $Schema).Path)
$settings = New-Object System.Xml.XmlReaderSettings
$settings.ValidationType = [System.Xml.ValidationType]::Schema
$settings.Schemas = $schemas
$problems = New-Object System.Collections.Generic.List[string]
$settings.add_ValidationEventHandler({ param($sender, $e) $problems.Add("$($e.Severity): $($e.Message)") })
$reader = [System.Xml.XmlReader]::Create((Resolve-Path $File).Path, $settings)
try { while ($reader.Read()) {} } finally { $reader.Dispose() }
if ($problems.Count -gt 0) {
  $problems | Select-Object -First 20 | ForEach-Object { Write-Host $_ }
  Write-Host "INVALID: $($problems.Count) problem(s) in $File"
  exit 1
}
Write-Host "VALID: $File conforms to LandXML 1.2"
