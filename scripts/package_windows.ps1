[CmdletBinding()]
param(
    [string]$QtRoot,
    [string]$OutputDirectory,
    [string]$InnoCompiler,
    [switch]$SkipInstaller
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$outRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'out'))

function Assert-UnderOut([string]$Path) {
    $fullPath = [IO.Path]::GetFullPath($Path)
    $prefix = $outRoot.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (-not $fullPath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to modify a path outside Moonmark's out directory: $fullPath"
    }
    return $fullPath
}

function Get-MoonmarkVersion {
    $metadataJson = & cargo metadata --format-version 1 --no-deps
    if ($LASTEXITCODE -ne 0) { throw 'cargo metadata failed.' }
    $metadata = $metadataJson | ConvertFrom-Json
    $manifestPath = [IO.Path]::GetFullPath((Join-Path $projectRoot 'Cargo.toml'))
    $package = $metadata.packages | Where-Object {
        [IO.Path]::GetFullPath($_.manifest_path) -eq $manifestPath
    } | Select-Object -First 1
    if (-not $package) { throw 'Moonmark package metadata was not found.' }
    return $package.version
}

function Get-NumericVersion([string]$Version) {
    if ($Version -notmatch '^(\d+)\.(\d+)\.(\d+)(?:-dev\.(\d+))?$') {
        throw "Moonmark version '$Version' cannot be represented as a Windows installer version."
    }
    $revision = if ($Matches[4]) { $Matches[4] } else { '0' }
    return "$($Matches[1]).$($Matches[2]).$($Matches[3]).$revision"
}

function Find-InnoCompiler {
    $candidates = @()
    if ($InnoCompiler) { $candidates += $InnoCompiler }
    if ($env:MOONMARK_INNO_ISCC) { $candidates += $env:MOONMARK_INNO_ISCC }
    $candidates += Get-ChildItem -LiteralPath (Join-Path $projectRoot 'out/toolchains/inno') `
        -Filter ISCC.exe -File -Recurse -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending | Select-Object -ExpandProperty FullName
    $candidates += @(
        'C:/Program Files/Inno Setup 7/ISCC.exe',
        'C:/Program Files (x86)/Inno Setup 7/ISCC.exe',
        'C:/Program Files/Inno Setup 6/ISCC.exe',
        'C:/Program Files (x86)/Inno Setup 6/ISCC.exe'
    )
    $command = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($command) { $candidates += $command.Source }
    $selected = $candidates | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) } |
        Select-Object -First 1
    if (-not $selected) {
        throw 'Inno Setup compiler not found. Run: pwsh -File scripts/bootstrap_inno.ps1'
    }
    return [IO.Path]::GetFullPath($selected)
}

function Find-VcRedistDirectory {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
        throw 'vswhere.exe was not found. Install the Visual Studio C++ build tools.'
    }

    $installations = & $vswhere -all -prerelease -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json |
        ConvertFrom-Json
    if ($LASTEXITCODE -ne 0 -or -not $installations) {
        throw 'No Visual Studio installation with the C++ x64 tools was found.'
    }

    $candidates = foreach ($installation in $installations) {
        $redistRoot = Join-Path $installation.installationPath 'VC/Redist/MSVC'
        if (-not (Test-Path -LiteralPath $redistRoot -PathType Container)) { continue }
        foreach ($versionDirectory in Get-ChildItem -LiteralPath $redistRoot -Directory) {
            if ($versionDirectory.Name -notmatch '^\d+(\.\d+)+$') { continue }
            foreach ($crtDirectory in Get-ChildItem -LiteralPath (Join-Path $versionDirectory.FullName 'x64') `
                    -Directory -Filter 'Microsoft.VC*.CRT' -ErrorAction SilentlyContinue) {
                [pscustomobject]@{
                    Version = [version]$versionDirectory.Name
                    Path = $crtDirectory.FullName
                }
            }
        }
    }
    $selected = $candidates | Sort-Object Version -Descending | Select-Object -First 1
    if (-not $selected) { throw 'An app-local x64 MSVC runtime directory was not found.' }
    return $selected.Path
}

Push-Location $projectRoot
try {
    $version = Get-MoonmarkVersion
    $numericVersion = Get-NumericVersion $version
    $packageRoot = [IO.Path]::GetFullPath((Join-Path $outRoot "package/staging/$version/Moonmark"))
    if (-not $QtRoot) {
        $QtRoot = if ($env:MOONMARK_QT_DIR) { $env:MOONMARK_QT_DIR } else {
            Join-Path $projectRoot 'out/toolchains/qt'
        }
    }
    $QtRoot = [IO.Path]::GetFullPath($QtRoot)
    if (-not $OutputDirectory) {
        $OutputDirectory = Join-Path $outRoot "release/$version"
    }
    $OutputDirectory = Assert-UnderOut $OutputDirectory
    $packageRoot = Assert-UnderOut $packageRoot
    $zipPath = Join-Path $OutputDirectory 'Moonmark-portable-win-x64.zip'
    $installerPath = Join-Path $OutputDirectory 'Moonmark-Setup-win-x64.exe'
    $checksumPath = Join-Path $OutputDirectory 'SHA256SUMS.txt'

    & cargo build --release
    if ($LASTEXITCODE -ne 0) { throw 'Moonmark release build failed.' }

    if (Test-Path -LiteralPath $packageRoot) {
        Remove-Item -Recurse -Force -LiteralPath $packageRoot
    }
    New-Item -ItemType Directory -Force -Path `
        (Join-Path $packageRoot 'platforms'), `
        (Join-Path $packageRoot 'assets/branding'), `
        (Join-Path $packageRoot 'assets/icons'), `
        (Join-Path $packageRoot 'licenses'), `
        $OutputDirectory | Out-Null

    Copy-Item -LiteralPath 'out/cargo/release/moonmark.exe' -Destination (Join-Path $packageRoot 'Moonmark.exe')
    foreach ($name in 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll') {
        $source = Join-Path $QtRoot "bin/$name"
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            throw "Required Qt runtime file is missing: $source"
        }
        Copy-Item -LiteralPath $source -Destination $packageRoot
    }
    $platformPlugin = Join-Path $QtRoot 'plugins/platforms/qwindows.dll'
    if (-not (Test-Path -LiteralPath $platformPlugin -PathType Leaf)) {
        throw "Required Qt platform plugin is missing: $platformPlugin"
    }
    Copy-Item -LiteralPath $platformPlugin -Destination (Join-Path $packageRoot 'platforms')
    Copy-Item -LiteralPath 'assets/branding/moonmark-symbol.png' -Destination (Join-Path $packageRoot 'assets/branding')
    Copy-Item -LiteralPath 'assets/icons/moonmark-markdown.ico' -Destination (Join-Path $packageRoot 'assets/icons')
    Copy-Item -LiteralPath 'assets/icons/moonmark-text.ico' -Destination (Join-Path $packageRoot 'assets/icons')
    Copy-Item -LiteralPath 'assets/deployment/qt.conf' -Destination (Join-Path $packageRoot 'qt.conf')
    Copy-Item -LiteralPath 'THIRD_PARTY_NOTICES.txt' -Destination $packageRoot
    Copy-Item -LiteralPath 'LICENSE' -Destination $packageRoot
    Copy-Item -LiteralPath 'docs/licenses/Qt-LGPL-3.0-only.txt' -Destination (Join-Path $packageRoot 'licenses')
    Copy-Item -LiteralPath 'LICENSE' -Destination (Join-Path $packageRoot 'licenses/Qt-GPL-3.0-only.txt')

    $redist = Find-VcRedistDirectory
    foreach ($name in 'msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll', 'vcruntime140.dll', 'vcruntime140_1.dll') {
        $source = Join-Path $redist $name
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            throw "Required app-local MSVC runtime file is missing: $source"
        }
        Copy-Item -LiteralPath $source -Destination $packageRoot
    }

    $savedPath = $env:PATH
    $savedQtDir = $env:QTDIR
    $savedMoonmarkQtDir = $env:MOONMARK_QT_DIR
    try {
        $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
        $env:QTDIR = $null
        $env:MOONMARK_QT_DIR = $null
        $smoke = Start-Process -FilePath (Join-Path $packageRoot 'Moonmark.exe') `
            -ArgumentList '--smoke-icon' -WorkingDirectory $packageRoot -Wait -PassThru -WindowStyle Hidden
        if ($smoke.ExitCode -ne 0) {
            throw "Packaged Moonmark smoke test failed with exit code $($smoke.ExitCode)."
        }
    } finally {
        $env:PATH = $savedPath
        $env:QTDIR = $savedQtDir
        $env:MOONMARK_QT_DIR = $savedMoonmarkQtDir
    }

    if (Test-Path -LiteralPath $zipPath) { Remove-Item -Force -LiteralPath $zipPath }
    Compress-Archive -Path $packageRoot -DestinationPath $zipPath -CompressionLevel Optimal

    $artifacts = @()
    if (-not $SkipInstaller) {
        $compiler = Find-InnoCompiler
        if (Test-Path -LiteralPath $installerPath) {
            Remove-Item -Force -LiteralPath $installerPath
        }
        $installerScript = Join-Path $projectRoot 'packaging/windows/Moonmark.iss'
        & $compiler '/Qp' "/DMyAppVersion=$version" "/DMyNumericVersion=$numericVersion" `
            "/DSourceDir=$packageRoot" "/DOutputDir=$OutputDirectory" `
            "/DProjectRoot=$projectRoot" $installerScript
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $installerPath -PathType Leaf)) {
            throw 'Moonmark installer compilation failed.'
        }
        $artifacts += $installerPath
    }
    $artifacts += $zipPath
    $checksumLines = foreach ($artifact in $artifacts) {
        $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $artifact).Hash.ToLowerInvariant()
        "$hash *$([IO.Path]::GetFileName($artifact))"
    }
    Set-Content -LiteralPath $checksumPath -Value $checksumLines -Encoding utf8NoBOM

    $folderBytes = (Get-ChildItem -LiteralPath $packageRoot -File -Recurse | Measure-Object Length -Sum).Sum
    $zipBytes = (Get-Item -LiteralPath $zipPath).Length
    Write-Host ("Moonmark {0} portable folder: {1:N2} MiB at {2}" -f $version, ($folderBytes / 1MB), $packageRoot)
    Write-Host ("Portable ZIP: {0:N2} MiB at {1}" -f ($zipBytes / 1MB), $zipPath)
    if (-not $SkipInstaller) {
        $installerBytes = (Get-Item -LiteralPath $installerPath).Length
        Write-Host ("Installer: {0:N2} MiB at {1}" -f ($installerBytes / 1MB), $installerPath)
    }
    Write-Host "Checksums: $checksumPath"
} finally {
    Pop-Location
}
