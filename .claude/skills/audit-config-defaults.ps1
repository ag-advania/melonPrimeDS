# audit-config-defaults.ps1
#
# Cross-checks GetInt/GetDouble/GetBool("Metroid....") call sites under src/frontend/qt_sdl
# against the three typed default lists in Config.cpp (DefaultInts / DefaultDoubles / DefaultBools).
# See .claude/rules/repo-architecture.md "GetXxx Default Coverage Audit (Metroid keys)" for the
# rationale: each GetXxx() only consults its own typed list, so a key registered in the wrong
# list (or missing entirely) silently falls back to 0/false/0.0 at runtime.
#
# Output: five sections -- "GetXxx missing" (key used but not in the matching default list) and
# four "GetXxx in DefaultYyy" cross-list mismatch sections.
# Exit code: 1 if any section has entries, 0 if all sections are empty (fully covered, no
# cross-list mismatches).

$ErrorActionPreference = 'Stop'

# .claude/skills -> repo root
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$qtSdl    = Join-Path $repoRoot 'src/frontend/qt_sdl'
$cfgPath  = Join-Path $qtSdl 'Config.cpp'
$hudSchemaPath = Join-Path $qtSdl 'MelonPrimeHudPropSchema.inc'

$cfgLines = Get-Content $cfgPath
$ints    = [System.Collections.Generic.HashSet[string]]::new()
$doubles = [System.Collections.Generic.HashSet[string]]::new()
$bools   = [System.Collections.Generic.HashSet[string]]::new()
$state = ""
foreach ($line in $cfgLines) {
  if ($line -match "^\s*DefaultList<int>\s+DefaultInts") { $state = "int"; continue }
  if ($line -match "^\s*DefaultList<double>\s+DefaultDoubles") { $state = "double"; continue }
  if ($line -match "^\s*DefaultList<bool>\s+DefaultBools") { $state = "bool"; continue }
  if ($state -ne "" -and $line -match "^\s*};\s*$") { $state = ""; continue }
  if ($state -eq "") { continue }
  if ($line -match '\{"Instance\*\.Metroid\.([^"]+)"\s*,') {
    $k = "Metroid." + $matches[1]
    if ($state -eq "int")    { [void]$ints.Add($k) }
    if ($state -eq "double") { [void]$doubles.Add($k) }
    if ($state -eq "bool")   { [void]$bools.Add($k) }
  }
}

if (Test-Path -LiteralPath $hudSchemaPath) {
  $schemaKeyMacros = @{}
  foreach ($line in [System.IO.File]::ReadLines($hudSchemaPath)) {
    if ($line -match '#define\s+MP_HUD_PROP_KEY_(\w+)\s+"(Metroid\.Visual\.[^"]+)"') {
      $schemaKeyMacros[$matches[1]] = $matches[2]
    }
  }
  foreach ($line in [System.IO.File]::ReadLines($hudSchemaPath)) {
    foreach ($m in [regex]::Matches($line, '\bX\([^,]+,\s*([^,]+),\s*(Int|Bool|String|Double),')) {
      $keyToken = $m.Groups[1].Value.Trim()
      $key = $null
      if ($keyToken -match '^"(Metroid\.Visual\.[^"]+)"$') {
        $key = $matches[1]
      } elseif ($keyToken -match '^MP_HUD_PROP_KEY_(\w+)$') {
        $key = $schemaKeyMacros[$matches[1]]
      }
      if ([string]::IsNullOrWhiteSpace($key)) { continue }
      $type = $m.Groups[2].Value
      if ($type -eq "Int")    { [void]$ints.Add($key) }
      if ($type -eq "Double") { [void]$doubles.Add($key) }
      if ($type -eq "Bool")   { [void]$bools.Add($key) }
    }
  }
}

# Use Select-String (built-in) instead of ripgrep so this script has no external tool dependency.
$qtSdlFiles = Get-ChildItem -Path $qtSdl -Recurse -File -Include '*.cpp','*.h'

function Get-MetroidKeyUsages([string]$accessor, [System.IO.FileInfo[]]$files) {
    $pattern = $accessor + '\("(Metroid\.[^"]+)"\)'
    $found = [System.Collections.Generic.HashSet[string]]::new()
    foreach ($f in $files) {
        foreach ($line in [System.IO.File]::ReadLines($f.FullName)) {
            foreach ($m in [regex]::Matches($line, $pattern)) {
                [void]$found.Add($m.Groups[1].Value)
            }
        }
    }
    return @($found) | Sort-Object -Unique
}

$usageInt = Get-MetroidKeyUsages 'GetInt'    $qtSdlFiles
$usageDbl = Get-MetroidKeyUsages 'GetDouble' $qtSdlFiles
$usageBol = Get-MetroidKeyUsages 'GetBool'   $qtSdlFiles

$missingInt = @($usageInt | ? { -not $ints.Contains($_) })
$missingDbl = @($usageDbl | ? { -not $doubles.Contains($_) })
$missingBol = @($usageBol | ? { -not $bools.Contains($_) })

$intInDoubles = @($usageInt | ? { $doubles.Contains($_) })
$intInBools   = @($usageInt | ? { $bools.Contains($_) })
$dblInInts    = @($usageDbl | ? { $ints.Contains($_) })
$bolInInts    = @($usageBol | ? { $ints.Contains($_) })
$bolInDoubles = @($usageBol | ? { $doubles.Contains($_) })

"GetInt missing:";    $missingInt
"GetDouble missing:"; $missingDbl
"GetBool missing:";   $missingBol

"GetInt in DefaultDoubles:";  $intInDoubles
"GetInt in DefaultBools:";    $intInBools
"GetDouble in DefaultInts:";  $dblInInts
"GetBool in DefaultInts:";    $bolInInts
"GetBool in DefaultDoubles:"; $bolInDoubles

$total = $missingInt.Count + $missingDbl.Count + $missingBol.Count `
       + $intInDoubles.Count + $intInBools.Count + $dblInInts.Count `
       + $bolInInts.Count + $bolInDoubles.Count

if ($total -gt 0) {
    Write-Host "FAIL: $total finding(s) across the sections above."
    exit 1
}
Write-Host "PASS: all Metroid.* GetInt/GetDouble/GetBool keys are covered with no cross-list mismatches."
exit 0
