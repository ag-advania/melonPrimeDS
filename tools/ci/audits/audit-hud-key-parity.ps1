# audit-hud-key-parity.ps1
#
# Dumps Metroid.* defaults from Config.cpp and compares HUD-facing
# Metroid.Visual.* key references across the current hand-mirrored surfaces:
# settings dialog tables, in-game edit descriptors, Qt edit side panel, and
# runtime Load*Config code.
#
# This is intentionally a source audit, not a C++ parser. It extracts string
# literals and expands the known generated HUD key families used today:
# color ramps, per-weapon icon tint keys, and per-element outline keys.
#
# Optional TOML mode:
#   .\tools\ci\audits\audit-hud-key-parity.ps1 -BeforeToml old.toml -AfterToml new.toml
# compares two saved config files and reports raw line differences.

param(
    [string]$BeforeToml = "",
    [string]$AfterToml = "",
    [int]$MaxList = 200,
    [switch]$Json,
    [switch]$Strict
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$qtSdl = Join-Path $repoRoot 'src/frontend/qt_sdl'
$configPath = Join-Path $qtSdl 'Config.cpp'
$hudSchemaPath = Join-Path $qtSdl 'MelonPrimeHudPropSchema.inc'
$hudDialogPropsPath = Join-Path $qtSdl 'InputConfig/MelonPrimeInputConfigHudDialogProps.inc'
$hudEditPropsPath = Join-Path $qtSdl 'MelonPrimeHudConfigOnScreenEditProps.inc'

function New-Set {
    return ,[System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
}

function Add-Key {
    param(
        [System.Collections.Generic.HashSet[string]]$Set,
        [string]$Key
    )
    if ([string]::IsNullOrWhiteSpace($Key)) { return }
    if ($Key.Contains('%')) { return }
    if ($Key -notmatch '^Metroid\.Visual\.') { return }
    [void]$Set.Add($Key)
}

function Add-OutlineKeys {
    param(
        [System.Collections.Generic.HashSet[string]]$Set,
        [string]$Prefix
    )
    Add-Key $Set "Metroid.Visual.${Prefix}Outline"
    Add-Key $Set "Metroid.Visual.${Prefix}OutlineColorR"
    Add-Key $Set "Metroid.Visual.${Prefix}OutlineColorG"
    Add-Key $Set "Metroid.Visual.${Prefix}OutlineColorB"
    Add-Key $Set "Metroid.Visual.${Prefix}OutlineOpacity"
    Add-Key $Set "Metroid.Visual.${Prefix}OutlineThickness"
}

function Add-RampKeys {
    param(
        [System.Collections.Generic.HashSet[string]]$Set,
        [string]$Prefix
    )
    Add-Key $Set "${Prefix}Enable"
    Add-Key $Set "${Prefix}Count"
    for ($i = 1; $i -le 6; $i++) {
        Add-Key $Set "${Prefix}${i}Pct"
        Add-Key $Set "${Prefix}${i}R"
        Add-Key $Set "${Prefix}${i}G"
        Add-Key $Set "${Prefix}${i}B"
    }
}

function Add-WeaponTintKeys {
    param(
        [System.Collections.Generic.HashSet[string]]$Set
    )
    $weapons = @(
        'PowerBeam', 'VoltDriver', 'Missile', 'BattleHammer', 'Imperialist',
        'Judicator', 'Magmaul', 'ShockCoil', 'OmegaCannon'
    )
    foreach ($w in $weapons) {
        Add-Key $Set "Metroid.Visual.HudWeaponIconColorOverlay$w"
        Add-Key $Set "Metroid.Visual.HudWeaponIconOverlayColorR$w"
        Add-Key $Set "Metroid.Visual.HudWeaponIconOverlayColorG$w"
        Add-Key $Set "Metroid.Visual.HudWeaponIconOverlayColorB$w"
    }
}

function Read-Text {
    param([string]$Path)
    return [System.IO.File]::ReadAllText($Path)
}

function Get-SchemaKeyMacros {
    $map = @{}
    if (-not (Test-Path -LiteralPath $hudSchemaPath)) { return $map }
    foreach ($line in [System.IO.File]::ReadLines($hudSchemaPath)) {
        if ($line -match '#define\s+MP_HUD_PROP_KEY_(\w+)\s+"(Metroid\.Visual\.[^"]+)"') {
            $map[$matches[1]] = $matches[2]
        }
    }
    return $map
}

function Extract-VisualLiterals {
    param(
        [string]$Text,
        [System.Collections.Generic.HashSet[string]]$Ignore = $null
    )
    $set = New-Set
    foreach ($m in [regex]::Matches($Text, '"(Metroid\.Visual\.[^"]+)"')) {
        $key = $m.Groups[1].Value
        if ($key.Contains('%')) { continue }
        if ($null -ne $Ignore -and $Ignore.Contains($key)) { continue }
        Add-Key $set $key
    }
    return ,$set
}

function Extract-Defaults {
    $defaults = @{}
    $counts = [ordered]@{
        Int = 0
        Bool = 0
        String = 0
        Double = 0
    }

    function Add-Default([string]$Key, [string]$Type, [string]$Value) {
        $defaults[$Key] = [pscustomobject]@{
            Key = $Key
            Type = $Type
            Value = $Value
        }
    }

    $state = ''
    foreach ($line in [System.IO.File]::ReadLines($configPath)) {
        if ($line -match '^\s*DefaultList<int>\s+DefaultInts') { $state = 'Int'; continue }
        if ($line -match '^\s*DefaultList<bool>\s+DefaultBools') { $state = 'Bool'; continue }
        if ($line -match '^\s*DefaultList<std::string>\s+DefaultStrings') { $state = 'String'; continue }
        if ($line -match '^\s*DefaultList<double>\s+DefaultDoubles') { $state = 'Double'; continue }
        if ($state -ne '' -and $line -match '^\s*};\s*$') { $state = ''; continue }
        if ($state -eq '') { continue }

        foreach ($m in [regex]::Matches($line, '\{\s*"Instance\*\.(Metroid\.[^"]+)"\s*,\s*([^}]+?)\s*\}')) {
            $key = $m.Groups[1].Value
            $value = $m.Groups[2].Value.Trim()
            Add-Default $key $state $value
        }
    }

    if (Test-Path -LiteralPath $hudSchemaPath) {
        $schemaKeyMacros = Get-SchemaKeyMacros
        foreach ($line in [System.IO.File]::ReadLines($hudSchemaPath)) {
            foreach ($m in [regex]::Matches($line, '\bX\([^,]+,\s*([^,]+),\s*(Int|Bool|String|Double),\s*([^,]+),')) {
                $keyToken = $m.Groups[1].Value.Trim()
                $key = $null
                if ($keyToken -match '^"(Metroid\.Visual\.[^"]+)"$') {
                    $key = $matches[1]
                } elseif ($keyToken -match '^MP_HUD_PROP_KEY_(\w+)$') {
                    $key = $schemaKeyMacros[$matches[1]]
                }
                if ([string]::IsNullOrWhiteSpace($key)) { continue }
                $type = $m.Groups[2].Value
                $value = $m.Groups[3].Value.Trim()
                Add-Default $key $type $value
            }
        }
    }

    foreach ($item in $defaults.Values) {
        $counts[$item.Type]++
    }

    return [pscustomobject]@{
        Items = $defaults
        Counts = $counts
    }
}

function Extract-DialogKeys {
    $path = Join-Path $qtSdl 'InputConfig/MelonPrimeInputConfig.cpp'
    $text = Read-Text $path
    $ignore = New-Set
    foreach ($m in [regex]::Matches($text, 'P_RAMP_STOP\("([1-6])",\s*"(Metroid\.Visual\.[^"]+)"\)')) {
        [void]$ignore.Add($m.Groups[2].Value)
    }

    $set = Extract-VisualLiterals $text $ignore
    if (Test-Path -LiteralPath $hudDialogPropsPath) {
        $dialogText = Read-Text $hudDialogPropsPath
        $schemaKeyMacros = Get-SchemaKeyMacros
        foreach ($m in [regex]::Matches($dialogText, '\bMP_HUD_PROP_KEY_(\w+)\b')) {
            $key = $schemaKeyMacros[$m.Groups[1].Value]
            if ($key) { Add-Key $set $key }
        }
        $literalKeys = Extract-VisualLiterals $dialogText
        foreach ($key in $literalKeys) { Add-Key $set $key }
    }
    foreach ($m in [regex]::Matches($text, 'P_RAMP_STOP\("([1-6])",\s*"(Metroid\.Visual\.[^"]+)"\)')) {
        $n = $m.Groups[1].Value
        $pfx = $m.Groups[2].Value
        Add-Key $set "${pfx}${n}Pct"
        Add-Key $set "${pfx}${n}R"
        Add-Key $set "${pfx}${n}G"
        Add-Key $set "${pfx}${n}B"
    }
    return ,$set
}

function Extract-EditDescriptorKeys {
    $path = Join-Path $qtSdl 'MelonPrimeHudConfigOnScreenDefs.inc'
    $set = Extract-VisualLiterals (Read-Text $path)
    if (Test-Path -LiteralPath $hudEditPropsPath) {
        $editText = Read-Text $hudEditPropsPath
        $schemaKeyMacros = Get-SchemaKeyMacros
        foreach ($m in [regex]::Matches($editText, '\bMP_HUD_PROP_KEY_(\w+)\b')) {
            $key = $schemaKeyMacros[$m.Groups[1].Value]
            if ($key) { Add-Key $set $key }
        }
        $literalKeys = Extract-VisualLiterals $editText
        foreach ($key in $literalKeys) { Add-Key $set $key }
    }
    return ,$set
}

function Extract-SidePanelKeys {
    $path = Join-Path $qtSdl 'MelonPrimeHudConfigOnScreenEdit.cpp'
    $text = Read-Text $path
    $set = Extract-VisualLiterals $text
    $schemaKeyMacros = Get-SchemaKeyMacros
    foreach ($m in [regex]::Matches($text, '\bMP_HUD_PROP_KEY_(\w+)\b')) {
        $key = $schemaKeyMacros[$m.Groups[1].Value]
        if ($key) { Add-Key $set $key }
    }

    foreach ($m in [regex]::Matches($text, 'addOutlineGroup\(MP_OUTLINE_KEYS\(\s*(\w+)\s*\)\)')) {
        Add-OutlineKeys $set $m.Groups[1].Value
    }
    # Per-weapon tint keys are now MP_HUD_PROP_KEY_* macros (resolved by the macro
    # pass above), not a "...%s" snprintf loop.

    return ,$set
}

function Extract-RuntimeLoadKeys {
    $path = Join-Path $qtSdl 'MelonPrimeHudRenderConfig.inc'
    $text = Read-Text $path
    $schemaKeyMacros = Get-SchemaKeyMacros
    $ignore = New-Set
    foreach ($m in [regex]::Matches($text, 'LoadColorRamp\([^;]+,\s*"((?:Metroid\.Visual\.)[^"]+)"\)')) {
        [void]$ignore.Add($m.Groups[1].Value)
    }

    $set = Extract-VisualLiterals $text $ignore

    foreach ($m in [regex]::Matches($text, 'LoadColorRamp\([^;]+,\s*"((?:Metroid\.Visual\.)[^"]+)"\)')) {
        Add-RampKeys $set $m.Groups[1].Value
    }
    foreach ($m in [regex]::Matches($text, 'loadOL\([^,]+,\s*"([^"]+)"\)')) {
        Add-OutlineKeys $set $m.Groups[1].Value
    }
    if ($text -match 'HudWeaponIconColorOverlay%s') {
        Add-WeaponTintKeys $set
    }
    foreach ($m in [regex]::Matches($text, '\bMP_HUD_PROP_KEY_(\w+)\b')) {
        $key = $schemaKeyMacros[$m.Groups[1].Value]
        if ($key) { Add-Key $set $key }
    }
    foreach ($m in [regex]::Matches($text, '\bMP_HUD_RAMP_KEYS\(\s*(\w+)\s*\)')) {
        $prefix = $m.Groups[1].Value
        if ($prefix -ne 'prefix') {
            Add-RampKeys $set "Metroid.Visual.$prefix"
        }
    }
    foreach ($m in [regex]::Matches($text, '\bMP_HUD_OUTLINE_KEYS\(\s*(\w+)\s*\)')) {
        $prefix = $m.Groups[1].Value
        if ($prefix -ne 'prefix') {
            Add-OutlineKeys $set $prefix
        }
    }

    return ,$set
}

function Set-ToSortedArray {
    param([System.Collections.Generic.HashSet[string]]$Set)
    return @($Set) | Sort-Object -Unique
}

function Diff-Keys {
    param(
        [string[]]$Left,
        [string[]]$Right
    )
    $rightSet = New-Set
    foreach ($k in $Right) { [void]$rightSet.Add($k) }
    return @($Left | Where-Object { -not $rightSet.Contains($_) })
}

function Write-ListSection {
    param(
        [string]$Title,
        [string[]]$Items
    )
    Write-Host "$Title ($($Items.Count))"
    foreach ($item in ($Items | Select-Object -First $MaxList)) {
        Write-Host "  $item"
    }
    if ($Items.Count -gt $MaxList) {
        Write-Host "  ... $($Items.Count - $MaxList) more"
    }
}

function Compare-TomlFiles {
    param(
        [string]$Before,
        [string]$After
    )
    if (-not (Test-Path -LiteralPath $Before)) { throw "BeforeToml not found: $Before" }
    if (-not (Test-Path -LiteralPath $After)) { throw "AfterToml not found: $After" }

    $beforeLines = Get-Content -LiteralPath $Before
    $afterLines = Get-Content -LiteralPath $After
    $diff = @(Compare-Object -ReferenceObject $beforeLines -DifferenceObject $afterLines -SyncWindow 3)
    if ($diff.Count -eq 0) {
        Write-Host "TOML diff: no raw line differences."
        return 0
    }

    Write-Host "TOML diff: $($diff.Count) raw line difference(s)."
    foreach ($d in ($diff | Select-Object -First $MaxList)) {
        $prefix = if ($d.SideIndicator -eq '<=') { '-' } else { '+' }
        Write-Host "$prefix $($d.InputObject)"
    }
    if ($diff.Count -gt $MaxList) {
        Write-Host "... $($diff.Count - $MaxList) more"
    }
    return $diff.Count
}

$tomlDiffCount = 0
if ($BeforeToml -ne '' -or $AfterToml -ne '') {
    if ($BeforeToml -eq '' -or $AfterToml -eq '') {
        throw 'Use both -BeforeToml and -AfterToml.'
    }
    $tomlDiffCount = Compare-TomlFiles $BeforeToml $AfterToml
}

$defaultsResult = Extract-Defaults
$defaults = $defaultsResult.Items
$metroidDefaultKeys = @($defaults.Keys | Sort-Object -Unique)
$visualDefaultKeys = @($metroidDefaultKeys | Where-Object { $_ -match '^Metroid\.Visual\.' })

$faces = [ordered]@{
    Dialog = Set-ToSortedArray (Extract-DialogKeys)
    EditDescriptors = Set-ToSortedArray (Extract-EditDescriptorKeys)
    SidePanel = Set-ToSortedArray (Extract-SidePanelKeys)
    RuntimeLoad = Set-ToSortedArray (Extract-RuntimeLoadKeys)
}

$faceReports = [ordered]@{}
foreach ($name in $faces.Keys) {
    $keys = $faces[$name]
    $faceReports[$name] = [pscustomobject]@{
        Count = $keys.Count
        MissingDefaults = Diff-Keys $keys $visualDefaultKeys
        DefaultsOnly = Diff-Keys $visualDefaultKeys $keys
    }
}

$pairReports = [ordered]@{}
$faceNames = @($faces.Keys)
for ($i = 0; $i -lt $faceNames.Count; $i++) {
    for ($j = $i + 1; $j -lt $faceNames.Count; $j++) {
        $a = $faceNames[$i]
        $b = $faceNames[$j]
        $pairReports["$a -> $b"] = Diff-Keys $faces[$a] $faces[$b]
        $pairReports["$b -> $a"] = Diff-Keys $faces[$b] $faces[$a]
    }
}

$findingCount = 0
foreach ($name in $faceReports.Keys) {
    $findingCount += $faceReports[$name].MissingDefaults.Count
}
$findingCount += $tomlDiffCount

if ($Json) {
    [pscustomobject]@{
        Defaults = [pscustomobject]@{
            MetroidCount = $metroidDefaultKeys.Count
            VisualCount = $visualDefaultKeys.Count
            TypeCounts = $defaultsResult.Counts
            Items = @($defaults.Values | Sort-Object Key)
        }
        Faces = $faces
        FaceReports = $faceReports
        PairReports = $pairReports
        TomlDiffCount = $tomlDiffCount
    } | ConvertTo-Json -Depth 6
} else {
    Write-Host "HUD key parity audit"
    Write-Host "Repo: $repoRoot"
    Write-Host "Metroid.* defaults: $($metroidDefaultKeys.Count)"
    Write-Host "Metroid.Visual.* defaults: $($visualDefaultKeys.Count)"
    Write-Host "Default type counts: Int=$($defaultsResult.Counts.Int), Bool=$($defaultsResult.Counts.Bool), String=$($defaultsResult.Counts.String), Double=$($defaultsResult.Counts.Double)"
    Write-Host ""

    foreach ($name in $faces.Keys) {
        $report = $faceReports[$name]
        Write-Host "$name keys: $($report.Count)"
        Write-ListSection "  $name references missing from Config.cpp defaults" $report.MissingDefaults
        Write-ListSection "  Config.cpp defaults not referenced by $name" $report.DefaultsOnly
        Write-Host ""
    }

    Write-Host "Face-to-face drift"
    foreach ($name in $pairReports.Keys) {
        Write-ListSection "  $name" $pairReports[$name]
    }
}

if ($Strict -and $findingCount -gt 0) {
    exit 1
}
exit 0
