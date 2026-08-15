[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $Rom,

    [string] $State,

    [string] $BuildDir = 'build\release-mingw-x86_64',

    [ValidateRange(1, 1000)]
    [int] $Iterations = 20,

    [ValidateRange(50, 10000)]
    [int] $IntervalMs = 100,

    [ValidateRange(5, 600)]
    [int] $Seconds = 20
)

$ErrorActionPreference = 'Stop'

function Resolve-RepoPath([string] $PathValue) {
    if ([IO.Path]::IsPathRooted($PathValue)) {
        return [IO.Path]::GetFullPath($PathValue)
    }
    return [IO.Path]::GetFullPath((Join-Path $repoRoot $PathValue))
}

function Count-Matches([string] $Text, [string] $Pattern) {
    return [regex]::Matches($Text, $Pattern, [Text.RegularExpressions.RegexOptions]::Multiline).Count
}

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$romPath = Resolve-RepoPath $Rom
$buildPath = Resolve-RepoPath $BuildDir
$appPath = Join-Path $buildPath 'melonPrimeDS.exe'

if (-not (Test-Path -LiteralPath $romPath -PathType Leaf)) {
    throw "ROM was not found: $romPath"
}
if (-not (Test-Path -LiteralPath $appPath -PathType Leaf)) {
    throw "developer build executable was not found: $appPath"
}
if ($State) {
    $statePath = Resolve-RepoPath $State
    if (-not (Test-Path -LiteralPath $statePath -PathType Leaf)) {
        throw "savestate was not found: $statePath"
    }
}

$runDirectory = Join-Path ([IO.Path]::GetTempPath()) (
    'melonprime-vulkan-fallback-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $runDirectory | Out-Null
$stdoutPath = Join-Path $runDirectory 'stdout.log'
$stderrPath = Join-Path $runDirectory 'stderr.log'

$environmentNames = @(
    'MELONPRIME_FORCE_VULKAN_RENDERER',
    'MELONPRIME_TEST_FORCE_VULKAN_RUNTIME_FAILURE',
    'MELONPRIME_RENDERER_SWITCH_STRESS',
    'MELONPRIME_RENDERER_SWITCH_STRESS_ITERATIONS',
    'MELONPRIME_RENDERER_SWITCH_STRESS_INTERVAL_MS',
    'MELONPRIME_TEST_SAVESTATE',
    'NSUnbufferedIO'
)
$savedEnvironment = @{}
foreach ($name in $environmentNames) {
    $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}

$process = $null
$stoppedByRunner = $false
$exitCode = $null
$output = ''
$failures = [System.Collections.Generic.List[string]]::new()

try {
    [Environment]::SetEnvironmentVariable('MELONPRIME_FORCE_VULKAN_RENDERER', '1', 'Process')
    [Environment]::SetEnvironmentVariable(
        'MELONPRIME_TEST_FORCE_VULKAN_RUNTIME_FAILURE', '1', 'Process')
    [Environment]::SetEnvironmentVariable('MELONPRIME_RENDERER_SWITCH_STRESS', '1,0', 'Process')
    [Environment]::SetEnvironmentVariable(
        'MELONPRIME_RENDERER_SWITCH_STRESS_ITERATIONS', $Iterations.ToString(), 'Process')
    [Environment]::SetEnvironmentVariable(
        'MELONPRIME_RENDERER_SWITCH_STRESS_INTERVAL_MS', $IntervalMs.ToString(), 'Process')
    [Environment]::SetEnvironmentVariable('NSUnbufferedIO', 'YES', 'Process')
    if ($State) {
        [Environment]::SetEnvironmentVariable(
            'MELONPRIME_TEST_SAVESTATE', $statePath, 'Process')
    }
    else {
        [Environment]::SetEnvironmentVariable('MELONPRIME_TEST_SAVESTATE', $null, 'Process')
    }

    $arguments = @('--boot', 'always', $romPath)
    $process = Start-Process -FilePath $appPath `
        -ArgumentList $arguments `
        -WorkingDirectory (Split-Path -Parent $appPath) `
        -RedirectStandardOutput $stdoutPath `
        -RedirectStandardError $stderrPath `
        -PassThru

    $deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
    while (-not $process.HasExited -and [DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 100
    }

    if (-not $process.HasExited) {
        $stoppedByRunner = $true
        Stop-Process -Id $process.Id -Force
        $process.WaitForExit()
    }
    $exitCode = $process.ExitCode

    $stdout = if (Test-Path -LiteralPath $stdoutPath) {
        Get-Content -LiteralPath $stdoutPath -Raw
    } else { '' }
    $stderr = if (Test-Path -LiteralPath $stderrPath) {
        Get-Content -LiteralPath $stderrPath -Raw
    } else { '' }
    $output = $stdout + "`n" + $stderr
    Set-Content -LiteralPath (Join-Path $runDirectory 'combined.log') -Value $output -Encoding UTF8

    $expectedSwitches = 2 * $Iterations
    if ((Count-Matches $output '\[fallback-test\] forced Vulkan runtime failure injection count=1') -ne 1) {
        $failures.Add('forced Vulkan runtime failure injection was not observed exactly once')
    }
    if ((Count-Matches $output '\[Vulkan\] runtime failure reported:') -ne 1) {
        $failures.Add('Vulkan runtime failure latch was not reported exactly once')
    }
    if ((Count-Matches $output 'Renderer fallback requested=Vulkan actual=Software') -ne 1) {
        $failures.Add('Vulkan-to-Software fallback was not observed exactly once')
    }
    if ((Count-Matches $output 'Renderer selection requested=Vulkan presentation=Vulkan') -ne 1) {
        $failures.Add('Vulkan was selected more than once, or was never selected')
    }
    if ((Count-Matches $output ("\[switch-stress\] complete: {0}/{0} switches performed" -f $expectedSwitches)) -ne 1) {
        $failures.Add("renderer-switch stress did not complete $expectedSwitches/$expectedSwitches switches")
    }
    if (-not $stoppedByRunner) {
        $failures.Add("process exited before the ${Seconds}s liveness deadline (exit=$exitCode)")
    }
    if (-not $stoppedByRunner -and $exitCode -ne 0) {
        $failures.Add("process exited unexpectedly with status $exitCode")
    }
    if ($output -match '(?im)(QFATAL|ASSERT(?:ION)? FAILED|segmentation fault|access violation|unhandled exception)') {
        $failures.Add('crash/fatal diagnostics were present in the captured output')
    }
}
finally {
    if ($process -and -not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
    }
    foreach ($name in $environmentNames) {
        [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process')
    }
}

Write-Host "Captured fallback stress log: $(Join-Path $runDirectory 'combined.log')"
if ($failures.Count -gt 0) {
    Write-Error 'FAIL: Vulkan renderer fallback/panel lifetime stress'
    foreach ($failure in $failures) {
        Write-Error "- $failure"
    }
    $diagnosticLines = $output -split "`r?`n" | Where-Object {
        $_ -match '(fallback-test|runtime failure|Renderer (selection|fallback|transition)|switch-stress|Vulkan.*(failed|failure|unavailable))'
    }
    $diagnosticLines | Select-Object -First 100 | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Host "PASS: Vulkan fallback latch, single fallback, $([int](2 * $Iterations)) renderer switches, and ${Seconds}s process liveness"
