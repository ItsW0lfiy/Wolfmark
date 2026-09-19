[CmdletBinding(SupportsShouldProcess)]
param([switch]$Deep)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$projectPrefix = $projectRoot.TrimEnd([IO.Path]::DirectorySeparatorChar) +
    [IO.Path]::DirectorySeparatorChar

function Remove-ProjectPath([string]$RelativePath) {
    $path = [IO.Path]::GetFullPath((Join-Path $projectRoot $RelativePath))
    if (-not $path.StartsWith($projectPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to clean outside Moonmark: $path"
    }
    if ((Test-Path -LiteralPath $path) -and $PSCmdlet.ShouldProcess($path, 'Remove generated Moonmark output')) {
        Remove-Item -LiteralPath $path -Recurse -Force
        Write-Host "Removed $RelativePath"
    }
}

foreach ($relative in @(
    'out/cargo',
    'out/tests',
    'out/visual',
    'out/validation',
    'out/package',
    'out/release',
    'out/logs',
    'out/temp'
)) {
    Remove-ProjectPath $relative
}

if ($Deep) {
    foreach ($relative in @('out/toolchains', 'out/cache')) {
        Remove-ProjectPath $relative
    }
}
