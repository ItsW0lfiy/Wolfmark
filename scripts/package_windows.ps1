[CmdletBinding()]
param(
    [string]$QtRoot,
    [string]$OutputDirectory,
    [string]$NsisExecutable,
    [switch]$SkipInstaller,
    [switch]$AllowDirty,
    [ValidateSet('development', 'prerelease', 'stable')]
    [string]$Channel = 'development'
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$outRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'out'))

function Assert-UnderOut([string]$Path) {
    $fullPath = [IO.Path]::GetFullPath($Path)
    $prefix = $outRoot.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (-not $fullPath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to modify a path outside Wolfmark's out directory: $fullPath"
    }
    return $fullPath
}

function Get-WolfmarkVersion {
    $metadata = (& cargo metadata --format-version 1 --no-deps) | ConvertFrom-Json
    if ($LASTEXITCODE -ne 0) { throw 'cargo metadata failed.' }
    $manifestPath = [IO.Path]::GetFullPath((Join-Path $projectRoot 'Cargo.toml'))
    $package = $metadata.packages | Where-Object {
        [IO.Path]::GetFullPath($_.manifest_path) -eq $manifestPath
    } | Select-Object -First 1
    if (-not $package) { throw 'Wolfmark package metadata was not found.' }
    return $package.version
}

function Get-WindowsVersion([string]$Version) {
    if ($Version -notmatch '^(\d+)\.(\d+)\.(\d+)(?:-dev\.(\d+))?$') {
        throw "Wolfmark version '$Version' cannot be represented as a Windows file version."
    }
    $build = ([int]$Matches[3] * 1000) + $(if ($Matches[4]) { [int]$Matches[4] } else { 999 })
    return [pscustomobject]@{
        Dotted = "$($Matches[1]).$($Matches[2]).$build.0"
        Resource = "$($Matches[1]),$($Matches[2]),$build,0"
    }
}

function Get-BuildIdentity([string]$Version, [string]$BuildChannel) {
    $commit = (& git rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $commit -notmatch '^[0-9a-f]{40}$') { throw 'Unable to resolve the source commit.' }
    $dirtyLines = @(& git status --porcelain --untracked-files=all)
    if ($LASTEXITCODE -ne 0) { throw 'Unable to inspect Git worktree state.' }
    $dirty = $dirtyLines.Count -gt 0
    if ($dirty -and -not $AllowDirty) {
        throw 'Official packaging requires a clean worktree. Commit the changes or pass -AllowDirty for a clearly marked local test artifact.'
    }
    $timestamp = [long]((& git show -s --format=%ct HEAD).Trim())
    if ($LASTEXITCODE -ne 0) { throw 'Unable to read the source commit timestamp.' }
    $ancestors = @(& git rev-list HEAD)
    if ($LASTEXITCODE -ne 0) { throw 'Unable to read Git ancestry.' }
    $buildId = $commit.Substring(0, 12) + $(if ($dirty) { '-dirty' } else { '' })
    return [ordered]@{
        product = 'Wolfmark'
        version = $Version
        sourceCommit = $commit
        buildId = $buildId
        sourceTimestamp = $timestamp
        architecture = 'x86_64'
        channel = $BuildChannel
        dirty = $dirty
        ancestors = $ancestors
    }
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
    $candidates = foreach ($versionDirectory in Get-ChildItem -LiteralPath (Join-Path $VisualStudioRoot 'VC/Redist/MSVC') -Directory) {
        if ($versionDirectory.Name -notmatch '^\d+(\.\d+)+$') { continue }
        foreach ($crtDirectory in Get-ChildItem -LiteralPath (Join-Path $versionDirectory.FullName 'x64') -Directory -Filter 'Microsoft.VC*.CRT' -ErrorAction SilentlyContinue) {
            [pscustomobject]@{ Version = [version]$versionDirectory.Name; Path = $crtDirectory.FullName }
        }
    }
    $selected = $candidates | Sort-Object Version -Descending | Select-Object -First 1
    if (-not $selected) { throw 'An app-local x64 MSVC runtime directory was not found.' }
    return $selected.Path
}

function Find-Nsis {
    $candidates = @($NsisExecutable, $env:WOLFMARK_NSIS)
    $candidates += Join-Path $outRoot 'toolchains/nsis/makensis.exe'
    $command = Get-Command makensis.exe -ErrorAction SilentlyContinue
    if ($command) { $candidates += $command.Source }
    $selected = $candidates | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) } | Select-Object -First 1
    if (-not $selected) { throw 'NSIS 3.12 was not found. Run: cargo setup' }
    $reported = (& $selected /VERSION).Trim()
    if ($LASTEXITCODE -ne 0 -or $reported -ne 'v3.12') { throw "Wolfmark requires NSIS 3.12; $selected reported '$reported'." }
    return [IO.Path]::GetFullPath($selected)
}

function Invoke-MsBuild([string]$MsBuild, [string[]]$Arguments) {
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $MsBuild
    $start.WorkingDirectory = $projectRoot
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $start.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::Start($start)
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    Write-Host $stdout.Result -NoNewline
    if ($stderr.Result) { Write-Host $stderr.Result -NoNewline }
    if ($process.ExitCode -ne 0) { throw "Native setup UI build failed with exit code $($process.ExitCode)." }
}

function Copy-RequiredFile([string]$Source, [string]$Destination) {
    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) { throw "Required package file is missing: $Source" }
    Copy-Item -LiteralPath $Source -Destination $Destination
}

Push-Location $projectRoot
try {
    $version = Get-WolfmarkVersion
    $windowsVersion = Get-WindowsVersion $version
    $buildIdentity = Get-BuildIdentity $version $Channel
    if (-not $QtRoot) { $QtRoot = if ($env:WOLFMARK_QT_DIR) { $env:WOLFMARK_QT_DIR } else { Join-Path $outRoot 'toolchains/qt' } }
    $QtRoot = [IO.Path]::GetFullPath($QtRoot)
    if (-not $OutputDirectory) { $OutputDirectory = Join-Path $outRoot "release/$version" }
    $OutputDirectory = Assert-UnderOut $OutputDirectory
    $packageRoot = Assert-UnderOut (Join-Path $outRoot "package/staging/$version/Wolfmark")
    $setupRuntime = Assert-UnderOut (Join-Path $outRoot 'package/nsis/setup-runtime')
    $identityRoot = Assert-UnderOut (Join-Path $outRoot 'package/identity')
    $zipPath = Join-Path $OutputDirectory 'Wolfmark-portable-win-x64.zip'
    $setupPath = Join-Path $OutputDirectory 'Wolfmark-Setup-win-x64.exe'
    $checksumPath = Join-Path $OutputDirectory 'SHA256SUMS.txt'
    $visualStudio = Find-VisualStudio
    $redist = Find-VcRedistDirectory $visualStudio
    New-Item -ItemType Directory -Force $OutputDirectory, $identityRoot | Out-Null
    foreach ($name in 'Wolfmark-Setup-win-x64.exe', 'Wolfmark-win-x64.msi', 'Wolfmark-portable-win-x64.zip', 'SHA256SUMS.txt') {
        $stale = Join-Path $OutputDirectory $name
        if (Test-Path -LiteralPath $stale) { Remove-Item -Force -LiteralPath $stale }
    }

    $identityPath = Join-Path $identityRoot 'build-identity.json'
    $buildIdentity | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $identityPath -Encoding utf8NoBOM
    $env:WOLFMARK_SOURCE_COMMIT = $buildIdentity.sourceCommit
    $env:WOLFMARK_BUILD_ID = $buildIdentity.buildId
    $env:WOLFMARK_SOURCE_TIMESTAMP = [string]$buildIdentity.sourceTimestamp
    $env:WOLFMARK_BUILD_CHANNEL = $buildIdentity.channel
    & cargo build --release -p wolfmark -p wolfmark-maintenance
    if ($LASTEXITCODE -ne 0) { throw 'Wolfmark release build failed.' }

    foreach ($path in $packageRoot, $setupRuntime) {
        if (Test-Path -LiteralPath $path) { Remove-Item -Recurse -Force -LiteralPath $path }
    }
    New-Item -ItemType Directory -Force (Join-Path $packageRoot 'platforms'), (Join-Path $packageRoot 'assets/branding'), (Join-Path $packageRoot 'assets/icons'), (Join-Path $packageRoot 'licenses'), (Join-Path $setupRuntime 'platforms'), (Join-Path $setupRuntime 'assets/branding') | Out-Null
    Copy-RequiredFile 'out/cargo/release/wolfmark.exe' (Join-Path $packageRoot 'Wolfmark.exe')
    foreach ($name in 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6Network.dll') { Copy-RequiredFile (Join-Path $QtRoot "bin/$name") $packageRoot }
    Copy-RequiredFile (Join-Path $QtRoot 'plugins/platforms/qwindows.dll') (Join-Path $packageRoot 'platforms')
    Copy-RequiredFile 'assets/branding/wolfmark-symbol.png' (Join-Path $packageRoot 'assets/branding')
    Copy-RequiredFile 'assets/icons/wolfmark-markdown.ico' (Join-Path $packageRoot 'assets/icons')
    Copy-RequiredFile 'assets/icons/wolfmark-text.ico' (Join-Path $packageRoot 'assets/icons')
    Copy-RequiredFile 'assets/deployment/qt.conf' (Join-Path $packageRoot 'qt.conf')
    Copy-RequiredFile 'THIRD_PARTY_NOTICES.txt' $packageRoot
    Copy-RequiredFile 'LICENSE' $packageRoot
    Copy-RequiredFile 'docs/licenses/Qt-LGPL-3.0-only.txt' (Join-Path $packageRoot 'licenses')
    Copy-RequiredFile 'LICENSE' (Join-Path $packageRoot 'licenses/Qt-GPL-3.0-only.txt')
    Copy-RequiredFile 'docs/licenses/NSIS-zlib-license.txt' (Join-Path $packageRoot 'licenses')
    foreach ($name in 'msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll', 'vcruntime140.dll', 'vcruntime140_1.dll') { Copy-RequiredFile (Join-Path $redist $name) $packageRoot }

    $maintenance = 'out/cargo/release/WolfmarkMaintenance.exe'
    & $maintenance manifest --payload $packageRoot --identity $identityPath --output (Join-Path $packageRoot 'payload-manifest.json')
    if ($LASTEXITCODE -ne 0) { throw 'Payload manifest generation failed.' }

    $savedPath = $env:PATH
    try {
        $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
        $smoke = Start-Process -FilePath (Join-Path $packageRoot 'Wolfmark.exe') -ArgumentList '--smoke-icon' -WorkingDirectory $packageRoot -Wait -PassThru -WindowStyle Hidden
        if ($smoke.ExitCode -ne 0) { throw "Packaged Wolfmark smoke test failed with exit code $($smoke.ExitCode)." }
    } finally { $env:PATH = $savedPath }

    $artifacts = @()
    if (-not $SkipInstaller) {
        $nsis = Find-Nsis
        $msbuild = Join-Path $visualStudio 'MSBuild/Current/Bin/MSBuild.exe'
        $buildRoot = Assert-UnderOut (Join-Path $outRoot 'package/nsis/setup-build')
        if (Test-Path -LiteralPath $buildRoot) { Remove-Item -Recurse -Force -LiteralPath $buildRoot }
        New-Item -ItemType Directory -Force (Join-Path $buildRoot 'generated'), (Join-Path $buildRoot 'bin') | Out-Null
        Set-Content -LiteralPath (Join-Path $buildRoot 'generated/wolfmark_setup_version.h') -Encoding ascii -Value @(
            "#define WOLFMARK_SETUP_VERSION_COMMAS $($windowsVersion.Resource)",
            "#define WOLFMARK_SETUP_VERSION_STRING `"$version`""
        )
        Invoke-MsBuild $msbuild @(
            'packaging\windows\setup-ui\WolfmarkSetup.vcxproj', '/t:Build', '/p:Configuration=Release', '/p:Platform=x64',
            "/p:QtRoot=$QtRoot", "/p:ProjectRoot=$projectRoot", "/p:WolfmarkOutputDir=$(Join-Path $buildRoot 'bin')",
            "/p:WolfmarkIntermediateDir=$(Join-Path $buildRoot 'obj')", "/p:WolfmarkGeneratedDir=$(Join-Path $buildRoot 'generated')",
            '/m:1', '/nr:false', '/v:minimal'
        )
        Copy-RequiredFile (Join-Path $buildRoot 'bin/WolfmarkSetup.exe') $setupRuntime
        Copy-RequiredFile $maintenance $setupRuntime
        foreach ($name in 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll') { Copy-RequiredFile (Join-Path $QtRoot "bin/$name") $setupRuntime }
        Copy-RequiredFile (Join-Path $QtRoot 'plugins/platforms/qwindows.dll') (Join-Path $setupRuntime 'platforms')
        Copy-RequiredFile 'assets/branding/wolfmark-symbol.png' (Join-Path $setupRuntime 'assets/branding')
        Set-Content -LiteralPath (Join-Path $setupRuntime 'qt.conf') -Encoding ascii -Value @('[Paths]', 'Plugins=.')
        foreach ($name in 'msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll', 'vcruntime140.dll', 'vcruntime140_1.dll') { Copy-RequiredFile (Join-Path $redist $name) $setupRuntime }

        # Exercise real argument parsing, Qt/plugin loading, core detection and the
        # native window before packaging. The probe cannot perform an operation.
        $startupRoot = Assert-UnderOut (Join-Path $outRoot 'tests/package-setup-startup')
        New-Item -ItemType Directory -Force $startupRoot | Out-Null
        $startup = [Diagnostics.ProcessStartInfo]::new()
        $startup.FileName = Join-Path $setupRuntime 'WolfmarkSetup.exe'
        $startup.WorkingDirectory = $startupRoot
        $startup.UseShellExecute = $false
        $startup.Environment['PATH'] = "$env:SystemRoot\System32;$env:SystemRoot"
        $startup.Environment.Remove('WOLFMARK_SETUP_SMOKE_STATE') | Out-Null
        $startup.Environment.Remove('WOLFMARK_SETUP_SCREENSHOT') | Out-Null
        foreach ($argument in @(
            "--maintenance=$(Join-Path $setupRuntime 'WolfmarkMaintenance.exe')", "--payload=$packageRoot",
            "--manifest=$(Join-Path $packageRoot 'payload-manifest.json')",
            # Existing file placeholder; this read-only probe never deploys it.
            "--uninstaller=$(Join-Path $setupRuntime 'WolfmarkSetup.exe')", '--install-dir=C:\Program Files\Wolfmark',
            "--bootstrap-log=$(Join-Path $startupRoot 'bootstrap.log')", "--startup-check=$(Join-Path $startupRoot 'startup.png')"
        )) { $startup.ArgumentList.Add($argument) }
        $startupProcess = [Diagnostics.Process]::Start($startup)
        if (-not $startupProcess.WaitForExit(45000)) {
            $startupProcess.Kill($true)
            throw "Native setup startup timed out. See $startupRoot/bootstrap.log."
        }
        if ($startupProcess.ExitCode -ne 0) { throw "Native setup startup failed with exit code $($startupProcess.ExitCode). See $startupRoot/bootstrap.log." }
        & (Join-Path $PSScriptRoot 'test_setup_ui_contract.ps1') -QtRoot $QtRoot

        & $nsis /V3 "/DOUTPUT_EXE=$setupPath" "/DSETUP_RUNTIME=$setupRuntime" "/DPAYLOAD_ROOT=$packageRoot" "/DDISPLAY_VERSION=$version" "/DWINDOWS_VERSION=$($windowsVersion.Dotted)" 'packaging/windows/nsis/Wolfmark.nsi'
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $setupPath -PathType Leaf)) { throw 'Wolfmark NSIS setup compilation failed.' }
        $artifacts += $setupPath
    }

    $portableMarker = Join-Path $packageRoot 'portable.flag'
    try {
        Set-Content -LiteralPath $portableMarker -Value '' -Encoding utf8NoBOM
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
    Write-Host "Build identity: $($buildIdentity.buildId) ($($buildIdentity.sourceCommit))"
    Write-Host "Checksums: $checksumPath"
} finally {
    Remove-Item Env:WOLFMARK_SOURCE_COMMIT, Env:WOLFMARK_BUILD_ID, Env:WOLFMARK_SOURCE_TIMESTAMP, Env:WOLFMARK_BUILD_CHANNEL -ErrorAction SilentlyContinue
    Pop-Location
}
