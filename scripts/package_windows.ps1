[CmdletBinding()]
param(
    [string]$QtRoot,
    [string]$OutputDirectory,
    [string]$WixExecutable,
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
    $metadata = (& cargo metadata --format-version 1 --no-deps) | ConvertFrom-Json
    if ($LASTEXITCODE -ne 0) { throw 'cargo metadata failed.' }
    $manifestPath = [IO.Path]::GetFullPath((Join-Path $projectRoot 'Cargo.toml'))
    $package = $metadata.packages | Where-Object {
        [IO.Path]::GetFullPath($_.manifest_path) -eq $manifestPath
    } | Select-Object -First 1
    if (-not $package) { throw 'Moonmark package metadata was not found.' }
    return $package.version
}

function Get-WindowsVersions([string]$Version) {
    if ($Version -notmatch '^(\d+)\.(\d+)\.(\d+)(?:-dev\.(\d+))?$') {
        throw "Moonmark version '$Version' cannot be represented as a Windows installer version."
    }
    $build = ([int]$Matches[3] * 1000) + $(if ($Matches[4]) { [int]$Matches[4] } else { 999 })
    if ($build -gt 65535) { throw "Moonmark version '$Version' exceeds Windows Installer version limits." }
    return [pscustomobject]@{
        Msi = "$($Matches[1]).$($Matches[2]).$build"
        Bundle = "$($Matches[1]).$($Matches[2]).$build.0"
        Resource = "$($Matches[1]),$($Matches[2]),$build,0"
    }
}

function Find-Wix {
    $candidates = @($WixExecutable, $env:MOONMARK_WIX)
    $candidates += Join-Path $outRoot 'toolchains/wix/wix.exe'
    $command = Get-Command wix.exe -ErrorAction SilentlyContinue
    if ($command) { $candidates += $command.Source }
    $selected = $candidates | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) } | Select-Object -First 1
    if (-not $selected) { throw 'WiX Toolset 7.0.0 was not found. Run: cargo setup' }
    $selected = [IO.Path]::GetFullPath($selected)
    $reported = (& $selected --version).Trim()
    if ($LASTEXITCODE -ne 0 -or -not $reported.StartsWith('7.0.0', [StringComparison]::Ordinal)) {
        throw "Moonmark requires WiX Toolset 7.0.0; $selected reported '$reported'."
    }
    return $selected
}

function Find-VisualStudio {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
        throw 'vswhere.exe was not found. Install Visual Studio Build Tools with the C++ x64 tools.'
    }
    $installation = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
    if ($LASTEXITCODE -ne 0 -or -not $installation) { throw 'No Visual Studio C++ x64 toolchain was found.' }
    return [IO.Path]::GetFullPath($installation)
}

function Find-VcRedistDirectory([string]$VisualStudioRoot) {
    $redistRoot = Join-Path $VisualStudioRoot 'VC/Redist/MSVC'
    $candidates = foreach ($versionDirectory in Get-ChildItem -LiteralPath $redistRoot -Directory) {
        if ($versionDirectory.Name -notmatch '^\d+(\.\d+)+$') { continue }
        foreach ($crtDirectory in Get-ChildItem -LiteralPath (Join-Path $versionDirectory.FullName 'x64') -Directory -Filter 'Microsoft.VC*.CRT' -ErrorAction SilentlyContinue) {
            [pscustomobject]@{ Version = [version]$versionDirectory.Name; Path = $crtDirectory.FullName }
        }
    }
    $selected = $candidates | Sort-Object Version -Descending | Select-Object -First 1
    if (-not $selected) { throw 'An app-local x64 MSVC runtime directory was not found.' }
    return $selected.Path
}

function Invoke-MsBuild([string]$MsBuild, [string[]]$Arguments) {
    # Normalize environment-variable key casing because .NET Framework MSBuild
    # otherwise rejects hosts that expose both Path and PATH.
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $MsBuild
    $start.WorkingDirectory = $projectRoot
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $start.ArgumentList.Add($argument) }
    $cleanEnvironment = @{}
    foreach ($key in [Environment]::GetEnvironmentVariables().Keys) {
        $cleanEnvironment[[string]$key] = [Environment]::GetEnvironmentVariable([string]$key)
    }
    $start.Environment.Clear()
    foreach ($entry in $cleanEnvironment.GetEnumerator()) { $start.Environment[$entry.Key] = $entry.Value }
    $temp = Assert-UnderOut (Join-Path $outRoot 'tmp/msbuild')
    New-Item -ItemType Directory -Force $temp | Out-Null
    $start.Environment['TEMP'] = $temp
    $start.Environment['TMP'] = $temp
    $process = [Diagnostics.Process]::Start($start)
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    Write-Host $stdout.Result -NoNewline
    if ($stderr.Result) { Write-Host $stderr.Result -NoNewline }
    if ($process.ExitCode -ne 0) { throw "Native bootstrapper build failed with exit code $($process.ExitCode)." }
}

function Copy-RequiredFile([string]$Source, [string]$Destination) {
    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) { throw "Required package file is missing: $Source" }
    Copy-Item -LiteralPath $Source -Destination $Destination
}

Push-Location $projectRoot
try {
    $version = Get-MoonmarkVersion
    $windowsVersion = Get-WindowsVersions $version
    if (-not $QtRoot) { $QtRoot = if ($env:MOONMARK_QT_DIR) { $env:MOONMARK_QT_DIR } else { Join-Path $outRoot 'toolchains/qt' } }
    $QtRoot = [IO.Path]::GetFullPath($QtRoot)
    if (-not $OutputDirectory) { $OutputDirectory = Join-Path $outRoot "release/$version" }
    $OutputDirectory = Assert-UnderOut $OutputDirectory
    $packageRoot = Assert-UnderOut (Join-Path $outRoot "package/staging/$version/Moonmark")
    $zipPath = Join-Path $OutputDirectory 'Moonmark-portable-win-x64.zip'
    $msiPath = Join-Path $OutputDirectory 'Moonmark-win-x64.msi'
    $setupPath = Join-Path $OutputDirectory 'Moonmark-Setup-win-x64.exe'
    $checksumPath = Join-Path $OutputDirectory 'SHA256SUMS.txt'
    $visualStudio = Find-VisualStudio
    $redist = Find-VcRedistDirectory $visualStudio
    New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
    foreach ($name in 'Moonmark-Setup-win-x64.exe', 'Moonmark-Setup-win-x64.wixpdb', 'Moonmark-win-x64.msi', 'Moonmark-win-x64.wixpdb', 'Moonmark-portable-win-x64.zip', 'SHA256SUMS.txt') {
        $stale = Join-Path $OutputDirectory $name
        if (Test-Path -LiteralPath $stale) { Remove-Item -Force -LiteralPath $stale }
    }

    & cargo build --release
    if ($LASTEXITCODE -ne 0) { throw 'Moonmark release build failed.' }

    if (Test-Path -LiteralPath $packageRoot) { Remove-Item -Recurse -Force -LiteralPath $packageRoot }
    New-Item -ItemType Directory -Force (Join-Path $packageRoot 'platforms'), (Join-Path $packageRoot 'assets/branding'), (Join-Path $packageRoot 'assets/icons'), (Join-Path $packageRoot 'licenses'), $OutputDirectory | Out-Null
    Copy-RequiredFile 'out/cargo/release/moonmark.exe' (Join-Path $packageRoot 'Moonmark.exe')
    foreach ($name in 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6Network.dll') { Copy-RequiredFile (Join-Path $QtRoot "bin/$name") $packageRoot }
    Copy-RequiredFile (Join-Path $QtRoot 'plugins/platforms/qwindows.dll') (Join-Path $packageRoot 'platforms')
    Copy-RequiredFile 'assets/branding/moonmark-symbol.png' (Join-Path $packageRoot 'assets/branding')
    Copy-RequiredFile 'assets/icons/moonmark-markdown.ico' (Join-Path $packageRoot 'assets/icons')
    Copy-RequiredFile 'assets/icons/moonmark-text.ico' (Join-Path $packageRoot 'assets/icons')
    Copy-RequiredFile 'assets/deployment/qt.conf' (Join-Path $packageRoot 'qt.conf')
    Copy-RequiredFile 'THIRD_PARTY_NOTICES.txt' $packageRoot
    Copy-RequiredFile 'LICENSE' $packageRoot
    Copy-RequiredFile 'docs/licenses/Qt-LGPL-3.0-only.txt' (Join-Path $packageRoot 'licenses')
    Copy-RequiredFile 'LICENSE' (Join-Path $packageRoot 'licenses/Qt-GPL-3.0-only.txt')
    Copy-RequiredFile 'docs/licenses/WiX-OSMF-EULA.txt' (Join-Path $packageRoot 'licenses')
    foreach ($name in 'msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll', 'vcruntime140.dll', 'vcruntime140_1.dll') { Copy-RequiredFile (Join-Path $redist $name) $packageRoot }

    $savedPath = $env:PATH
    try {
        $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
        $smoke = Start-Process -FilePath (Join-Path $packageRoot 'Moonmark.exe') -ArgumentList '--smoke-icon' -WorkingDirectory $packageRoot -Wait -PassThru -WindowStyle Hidden
        if ($smoke.ExitCode -ne 0) { throw "Packaged Moonmark smoke test failed with exit code $($smoke.ExitCode)." }
    } finally { $env:PATH = $savedPath }

    $artifacts = @()
    if (-not $SkipInstaller) {
        $wix = Find-Wix
        $msbuild = Join-Path $visualStudio 'MSBuild/Current/Bin/MSBuild.exe'
        if (-not (Test-Path -LiteralPath $msbuild -PathType Leaf)) { throw "MSBuild was not found at $msbuild." }
        $wixRoot = Assert-UnderOut (Join-Path $outRoot 'package/wix')
        $baBuild = Join-Path $wixRoot 'bootstrapper-build'
        $baPayload = Join-Path $wixRoot 'bootstrapper-payload'
        $nugetPackages = Assert-UnderOut (Join-Path $outRoot 'cache/nuget')
        $baApi = Join-Path $nugetPackages 'wixtoolset.bootstrapperapplicationapi/7.0.0'
        if (-not (Test-Path -LiteralPath $baApi -PathType Container)) { throw 'WiX bootstrapper API packages are missing. Run: cargo setup' }
        if (Test-Path -LiteralPath $baBuild) { Remove-Item -Recurse -Force -LiteralPath $baBuild }
        if (Test-Path -LiteralPath $baPayload) { Remove-Item -Recurse -Force -LiteralPath $baPayload }
        $generated = Join-Path $baBuild 'generated'
        New-Item -ItemType Directory -Force $generated, (Join-Path $baPayload 'platforms') | Out-Null
        Set-Content -LiteralPath (Join-Path $generated 'moonmark_setup_version.h') -Encoding ascii -Value @(
            "#define MOONMARK_SETUP_VERSION_COMMAS $($windowsVersion.Resource)",
            "#define MOONMARK_SETUP_VERSION_STRING `"$version`""
        )
        Invoke-MsBuild $msbuild @(
            'packaging\windows\bootstrapper\MoonmarkSetup.vcxproj', '/t:Build', '/p:Configuration=Release', '/p:Platform=x64',
            "/p:QtRoot=$QtRoot", "/p:ProjectRoot=$projectRoot", "/p:MoonmarkOutputDir=$(Join-Path $baBuild 'bin')",
            "/p:MoonmarkIntermediateDir=$(Join-Path $baBuild 'obj')", "/p:MoonmarkGeneratedDir=$generated",
            "/p:RestorePackagesPath=$nugetPackages", '/p:AcceptEula=wix7', '/m:1', '/nr:false', '/v:minimal'
        )
        $bootstrapperExe = Join-Path $baBuild 'bin/MoonmarkSetup.exe'
        if (-not (Test-Path -LiteralPath $bootstrapperExe -PathType Leaf)) { throw "Native bootstrapper is missing: $bootstrapperExe" }
        foreach ($name in 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll') { Copy-RequiredFile (Join-Path $QtRoot "bin/$name") $baPayload }
        Copy-RequiredFile (Join-Path $QtRoot 'plugins/platforms/qwindows.dll') (Join-Path $baPayload 'platforms')
        Set-Content -LiteralPath (Join-Path $baPayload 'qt.conf') -Encoding ascii -Value @('[Paths]', 'Plugins=.')
        Copy-RequiredFile 'docs/licenses/WiX-OSMF-EULA.txt' $baPayload
        foreach ($name in 'msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll', 'vcruntime140.dll', 'vcruntime140_1.dll') { Copy-RequiredFile (Join-Path $redist $name) $baPayload }

        $msiBuild = Join-Path $wixRoot 'msi'
        $bundleBuild = Join-Path $wixRoot 'bundle'
        New-Item -ItemType Directory -Force $msiBuild, $bundleBuild | Out-Null
        $builtMsi = Join-Path $msiBuild 'Moonmark-win-x64.msi'
        $builtSetup = Join-Path $bundleBuild 'Moonmark-Setup-win-x64.exe'
        & $wix build -acceptEula wix7 -arch x64 -bindpath "Payload=$packageRoot" -d "MsiVersion=$($windowsVersion.Msi)" -d "DisplayVersion=$version" -d "ProjectRoot=$projectRoot" -intermediatefolder (Join-Path $msiBuild 'obj') 'packaging/windows/wix/Moonmark.wxs' -o $builtMsi
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $builtMsi -PathType Leaf)) { throw 'Moonmark MSI compilation failed.' }
        & $wix build -acceptEula wix7 -arch x64 -bindpath "Bootstrapper=$baPayload" -d "BundleVersion=$($windowsVersion.Bundle)" -d "DisplayVersion=$version" -d "ProjectRoot=$projectRoot" -d "MsiPath=$builtMsi" -d "BootstrapperExe=$bootstrapperExe" -intermediatefolder (Join-Path $bundleBuild 'obj') 'packaging/windows/wix/Bundle.wxs' -o $builtSetup
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $builtSetup -PathType Leaf)) { throw 'Moonmark setup bundle compilation failed.' }
        Copy-Item -LiteralPath $builtMsi -Destination $msiPath
        Copy-Item -LiteralPath $builtSetup -Destination $setupPath
        $artifacts += $setupPath, $msiPath
    }

    $portableMarker = Join-Path $packageRoot 'portable.flag'
    try {
        Set-Content -LiteralPath $portableMarker -Value '' -Encoding utf8NoBOM
        if (Test-Path -LiteralPath $zipPath) { Remove-Item -Force -LiteralPath $zipPath }
        Compress-Archive -Path $packageRoot -DestinationPath $zipPath -CompressionLevel Optimal
    } finally { if (Test-Path -LiteralPath $portableMarker) { Remove-Item -Force -LiteralPath $portableMarker } }
    $artifacts += $zipPath
    $checksumLines = foreach ($artifact in $artifacts) {
        "{0} *{1}" -f (Get-FileHash -Algorithm SHA256 -LiteralPath $artifact).Hash.ToLowerInvariant(), [IO.Path]::GetFileName($artifact)
    }
    Set-Content -LiteralPath $checksumPath -Value $checksumLines -Encoding utf8NoBOM

    foreach ($artifact in $artifacts) {
        $item = Get-Item -LiteralPath $artifact
        Write-Host ("{0}: {1:N2} MiB" -f $item.Name, ($item.Length / 1MB))
    }
    Write-Host "Checksums: $checksumPath"
} finally {
    Pop-Location
}
