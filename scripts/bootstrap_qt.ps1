[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$outRoot = Join-Path $projectRoot 'out'
$sdkRoot = Join-Path $outRoot 'toolchains/qt'
$archiveRoot = Join-Path $outRoot 'cache/qt'
$version = '6.11.2'
$stamp = '6.11.2-0-202608131017'
$archive = 'qtbase-Windows-Windows_11_24H2-MSVC2022-Windows-Windows_11_24H2-X86_64.7z'
$package = 'qt.qt6.6112.win64_msvc2022_64'
$baseUrl = "https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_6112/qt6_6112_msvc2022_64/$package"
$archivePath = Join-Path $archiveRoot "$stamp$archive"

if (Test-Path (Join-Path $sdkRoot 'lib/Qt6Widgets.lib')) {
    Write-Host "Qt $version is already available at $sdkRoot"
    exit 0
}

New-Item -ItemType Directory -Force -Path $archiveRoot, $sdkRoot | Out-Null
& curl.exe --fail --location --show-error --output $archivePath "$baseUrl/$stamp$archive"
if ($LASTEXITCODE -ne 0) { throw 'Qt SDK download failed.' }

$expectedPath = "$archivePath.sha1"
& curl.exe --fail --location --silent --show-error --output $expectedPath "$baseUrl/$stamp$archive.sha1"
if ($LASTEXITCODE -ne 0) { throw 'Qt checksum download failed.' }
$expected = (Get-Content -Raw -LiteralPath $expectedPath).Trim().Split(' ')[0].ToLowerInvariant()
$actual = (Get-FileHash -Algorithm SHA1 -LiteralPath $archivePath).Hash.ToLowerInvariant()
if ($actual -ne $expected) { throw "Qt SDK checksum mismatch: expected $expected, got $actual" }

& tar.exe -xf $archivePath -C $sdkRoot
if ($LASTEXITCODE -ne 0) { throw 'Qt SDK extraction failed.' }
if (-not (Test-Path (Join-Path $sdkRoot 'lib/Qt6Widgets.lib'))) {
    throw 'The extracted Qt SDK does not contain Qt6Widgets.lib.'
}
Write-Host "Qt $version installed project-locally at $sdkRoot"
