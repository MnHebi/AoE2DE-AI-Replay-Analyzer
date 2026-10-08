param([string]$Python, [string]$Replay)
$ErrorActionPreference = 'Stop'
$launchTaskDir = $PSScriptRoot
if (-not $Python) { $Python = $env:AOE2_PYTHON }
if (-not $Python) {
    $bundledTaskPython = Join-Path $env:USERPROFILE '.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
    if (Test-Path -LiteralPath $bundledTaskPython) { $Python = $bundledTaskPython }
}
if ($Python) { $env:AOE2_PYTHON = $Python }
$launchTaskExe = Join-Path $launchTaskDir 'replay-analysis.exe'
if (-not (Test-Path -LiteralPath $launchTaskExe)) { $launchTaskExe = Join-Path $launchTaskDir 'dist\replay-analysis.exe' }
if (-not (Test-Path -LiteralPath $launchTaskExe)) { throw 'Build and install the application first; see README.md.' }
if ($Replay) { & $launchTaskExe $Replay } else { & $launchTaskExe }
