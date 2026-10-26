[CmdletBinding()]
param([switch]$AllowDirty)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$pwsh = (Get-Command pwsh -ErrorAction Stop).Source

function Invoke-CargoStage([string]$Name, [string[]]$Arguments) {
    Write-Host "`n==> $Name"
    & cargo @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Name failed with exit code $LASTEXITCODE."
    }
}

function Invoke-ScriptStage([string]$Name, [string]$Script, [string[]]$Arguments = @()) {
    Write-Host "`n==> $Name"
    & $pwsh -NoProfile -File (Join-Path $PSScriptRoot $Script) @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Name failed with exit code $LASTEXITCODE."
    }
}

Push-Location $projectRoot
try {
    Invoke-ScriptStage 'Preparing Wolfmark development tooling' 'setup.ps1'
    Invoke-CargoStage 'Checking Rust formatting' @('fmt', '--all', '--check')
    Invoke-CargoStage 'Checking the complete workspace' @('check', '--workspace')
    # Ignored stress fixtures are absent after a fresh clone or clean rebuild.
    Invoke-CargoStage 'Generating native test fixtures' @(
        'run', '--locked', '--bin', 'generate_stress_fixture'
    )
    Invoke-CargoStage 'Testing the complete workspace' @('test', '--workspace')
    Invoke-CargoStage 'Running strict workspace linting' @(
        'clippy', '--workspace', '--all-targets', '--all-features', '--', '-D', 'warnings'
    )
    Invoke-CargoStage 'Building the complete Release workspace' @('build', '--workspace', '--release')
    Invoke-CargoStage 'Running the Release application smoke' @(
        'run', '--release', '--bin', 'wolfmark', '--', '--smoke-icon'
    )
    $packageArguments = if ($AllowDirty) { @('-AllowDirty') } else { @() }
    Invoke-ScriptStage 'Building Wolfmark Windows distributables' 'package_windows.ps1' $packageArguments
    Write-Host "`nWolfmark setup, validation, build, smoke, and packaging all passed." -ForegroundColor Green
} catch {
    Write-Error "Wolfmark combined workflow failed: $($_.Exception.Message)"
    exit 1
} finally {
    Pop-Location
}
