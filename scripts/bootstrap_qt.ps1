[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$outRoot = Join-Path $projectRoot 'out'
$sdkRoot = Join-Path $outRoot 'toolchains/qt'
$archiveRoot = Join-Path $outRoot 'cache/qt'
$version = '6.8.2'
$stamp = '6.8.2-0-202501260838'
$archive = 'qtbase-Windows-Windows_11_23H2-MSVC2022-Windows-Windows_11_23H2-X86_64.7z'
$package = 'qt.qt6.682.win64_msvc2022_64'
$baseUrl = "https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_682/qt6_682/$package"
$archivePath = Join-Path $archiveRoot "$stamp$archive"

function Test-QtSdk([string]$Root = $sdkRoot) {
    foreach ($library in 'Qt6Core.lib', 'Qt6Gui.lib', 'Qt6Widgets.lib', 'Qt6Network.lib') {
        if (-not (Test-Path -LiteralPath (Join-Path $Root "lib/$library") -PathType Leaf)) {
            return $false
        }
    }
    # Prevent stale Qt 6.11.x SDKs from being mistaken for CI-verified 6.8.2.
    $core = Join-Path $Root 'bin/Qt6Core.dll'
    if (-not (Test-Path -LiteralPath $core -PathType Leaf)) { return $false }
    $actualVersion = (Get-Item -LiteralPath $core).VersionInfo.FileVersion
    return $actualVersion -match '^6\.8\.2(?:\D|$)'
}

if (Test-QtSdk) {
    Write-Host "Qt $version is already available at $sdkRoot"
    exit 0
}

New-Item -ItemType Directory -Force -Path $archiveRoot | Out-Null
& curl.exe --fail --location --show-error --output $archivePath "$baseUrl/$stamp$archive"
if ($LASTEXITCODE -ne 0) { throw 'Qt SDK download failed.' }

$expectedPath = "$archivePath.sha1"
& curl.exe --fail --location --silent --show-error --output $expectedPath "$baseUrl/$stamp$archive.sha1"
if ($LASTEXITCODE -ne 0) { throw 'Qt checksum download failed.' }
$expected = (Get-Content -Raw -LiteralPath $expectedPath).Trim().Split(' ')[0].ToLowerInvariant()
$actual = (Get-FileHash -Algorithm SHA1 -LiteralPath $archivePath).Hash.ToLowerInvariant()
if ($actual -ne $expected) { throw "Qt SDK checksum mismatch: expected $expected, got $actual" }

# Stage and validate before replacing the project-local SDK; never mix DLLs
# and headers from two Qt versions in the same directory.
$stagingRoot = Join-Path $outRoot 'toolchains/qt-staging'
if (Test-Path -LiteralPath $stagingRoot) {
    Remove-Item -LiteralPath $stagingRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $stagingRoot | Out-Null
& tar.exe -xf $archivePath -C $stagingRoot
if ($LASTEXITCODE -ne 0) { throw 'Qt SDK extraction failed.' }
if (-not (Test-QtSdk $stagingRoot)) {
    throw 'The extracted Qt SDK is incomplete or has the wrong Qt version.'
}
if (Test-Path -LiteralPath $sdkRoot) {
    Remove-Item -LiteralPath $sdkRoot -Recurse -Force
}
Move-Item -LiteralPath $stagingRoot -Destination $sdkRoot
Write-Host "Qt $version installed project-locally at $sdkRoot"
