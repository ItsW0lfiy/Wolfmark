[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$outRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'out'))
$toolRoot = [IO.Path]::GetFullPath((Join-Path $outRoot 'toolchains/nsis'))
$cacheRoot = [IO.Path]::GetFullPath((Join-Path $outRoot 'cache/nsis'))
$archive = Join-Path $cacheRoot 'nsis-3.12.zip'
$makensis = Join-Path $toolRoot 'makensis.exe'
$download = 'https://mirrors.mit.edu/macports/distfiles/nsis/nsis-3.12.zip'
$expectedSha256 = '56581f90db321581c5381193d796fffcf2d24b2f8fed2160a6c6a3baa67f2c4f'

function Assert-NsisVersion {
    if (-not (Test-Path -LiteralPath $makensis -PathType Leaf)) { return $false }
    $reported = (& $makensis /VERSION).Trim()
    if ($LASTEXITCODE -ne 0 -or $reported -ne 'v3.12') {
        throw "Unexpected NSIS compiler at $makensis (reported '$reported')."
    }
    return $true
}

Push-Location $projectRoot
try {
    if (Assert-NsisVersion) {
        Write-Host "NSIS 3.12 is already available at $makensis"
        exit 0
    }
    New-Item -ItemType Directory -Force $cacheRoot | Out-Null
    if (Test-Path -LiteralPath $archive -PathType Leaf) {
        $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $archive).Hash.ToLowerInvariant()
        if ($actual -ne $expectedSha256) { Remove-Item -Force -LiteralPath $archive }
    }
    if (-not (Test-Path -LiteralPath $archive -PathType Leaf)) {
        Write-Host 'Downloading the pinned NSIS 3.12 portable toolchain...'
        Invoke-WebRequest -Uri $download -OutFile $archive -UseBasicParsing
    }
    $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $archive).Hash.ToLowerInvariant()
    if ($actual -ne $expectedSha256) {
        throw "NSIS archive checksum mismatch. Expected $expectedSha256, received $actual."
    }
    $extractRoot = Join-Path $cacheRoot 'extract'
    if (Test-Path -LiteralPath $extractRoot) { Remove-Item -Recurse -Force -LiteralPath $extractRoot }
    if (Test-Path -LiteralPath $toolRoot) { Remove-Item -Recurse -Force -LiteralPath $toolRoot }
    Expand-Archive -LiteralPath $archive -DestinationPath $extractRoot
    $source = Get-ChildItem -LiteralPath $extractRoot -Directory | Where-Object {
        Test-Path -LiteralPath (Join-Path $_.FullName 'makensis.exe') -PathType Leaf
    } | Select-Object -First 1
    if (-not $source) { throw 'The verified NSIS archive did not contain makensis.exe.' }
    New-Item -ItemType Directory -Force $toolRoot | Out-Null
    Copy-Item -Path (Join-Path $source.FullName '*') -Destination $toolRoot -Recurse -Force
    if (-not (Assert-NsisVersion)) { throw 'NSIS bootstrap did not produce makensis.exe.' }
    Write-Host "NSIS 3.12 is ready at $makensis"
} catch {
    Write-Error "NSIS bootstrap failed: $($_.Exception.Message)"
    exit 1
} finally {
    Pop-Location
}
