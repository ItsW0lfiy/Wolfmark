[CmdletBinding()]
param(
    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$outRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'out'))
$runtime = Join-Path $outRoot 'package/wix/bootstrapper-smoke'
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $outRoot 'visual/wix-setup' }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (-not $OutputDirectory.StartsWith($outRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Screenshot output must stay under Wolfmark's out directory: $OutputDirectory"
}

Push-Location $projectRoot
try {
    $bootstrapper = Join-Path $outRoot 'package/wix/bootstrapper-build/bin/WolfmarkSetup.exe'
    $payload = Join-Path $outRoot 'package/wix/bootstrapper-payload'
    if (-not (Test-Path -LiteralPath $bootstrapper -PathType Leaf) -or -not (Test-Path -LiteralPath $payload -PathType Container)) {
        throw 'The native bootstrapper has not been built. Run cargo package-app first.'
    }
    if (Test-Path -LiteralPath $runtime) { Remove-Item -Recurse -Force -LiteralPath $runtime }
    Copy-Item -LiteralPath $payload -Destination $runtime -Recurse
    Copy-Item -LiteralPath $bootstrapper -Destination $runtime
    New-Item -ItemType Directory -Force $OutputDirectory | Out-Null

    foreach ($state in 'install', 'upgrade', 'maintenance', 'repair', 'uninstall', 'progress', 'complete', 'error') {
        $env:WOLFMARK_SETUP_SMOKE_STATE = $state
        $env:WOLFMARK_SETUP_SCREENSHOT = Join-Path $OutputDirectory "$state.png"
        $process = Start-Process -FilePath (Join-Path $runtime 'WolfmarkSetup.exe') -Wait -PassThru
        if ($process.ExitCode -ne 0) { throw "Installer UI smoke '$state' failed with exit code $($process.ExitCode)." }
    }
    Write-Host "Wolfmark installer UI screenshots: $OutputDirectory"
} finally {
    Remove-Item Env:WOLFMARK_SETUP_SMOKE_STATE, Env:WOLFMARK_SETUP_SCREENSHOT -ErrorAction SilentlyContinue
    Pop-Location
}
