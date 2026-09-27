[CmdletBinding()]
param([string]$OutputDirectory)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$runtime = Join-Path $projectRoot 'out/package/nsis/setup-runtime'
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $projectRoot 'out/visual/nsis-setup' }
$executable = Join-Path $runtime 'WolfmarkSetup.exe'
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw 'The native NSIS setup UI host has not been built. Run cargo package-app first.'
}
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
foreach ($state in 'install', 'update', 'maintenance', 'repair', 'uninstall', 'progress', 'complete', 'error') {
    $screenshot = [IO.Path]::GetFullPath((Join-Path $OutputDirectory "$state.png"))
    $process = Start-Process -FilePath $executable -ArgumentList "--ui-smoke=$state", "--ui-screenshot=$screenshot" -WorkingDirectory $runtime -Wait -PassThru
    if ($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $screenshot -PathType Leaf)) {
        throw "Setup UI capture failed for $state."
    }
}
Write-Host "NSIS setup UI screenshots: $OutputDirectory"
