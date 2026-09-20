[CmdletBinding()]
param(
    [string]$ReleaseDirectory,
    [switch]$ExecuteLifecycle,
    [switch]$AllowExistingMoonmarkReplacement
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$outRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'out'))

function Get-Version {
    $metadata = (& cargo metadata --format-version 1 --no-deps) | ConvertFrom-Json
    if ($LASTEXITCODE -ne 0) { throw 'cargo metadata failed.' }
    return ($metadata.packages | Where-Object name -eq 'moonmark' | Select-Object -First 1).version
}

function Invoke-Msi([string[]]$Arguments, [string]$LogPath) {
    $process = Start-Process -FilePath "$env:SystemRoot\System32\msiexec.exe" -ArgumentList ($Arguments + @('/qn', '/norestart', '/L*v', $LogPath)) -Wait -PassThru
    if ($process.ExitCode -notin 0, 3010) { throw "Windows Installer failed with exit code $($process.ExitCode). See $LogPath" }
}

function Invoke-Setup([string]$SetupPath, [string[]]$Arguments, [string]$LogPath) {
    $process = Start-Process -FilePath $SetupPath -ArgumentList ($Arguments + @('/quiet', '/norestart', '/log', $LogPath)) -Wait -PassThru
    if ($process.ExitCode -notin 0, 3010) { throw "Moonmark Setup failed with exit code $($process.ExitCode). See $LogPath" }
}

function Get-OlderInstallerVersion([string]$Version) {
    if ($Version -notmatch '^(\d+)\.(\d+)\.(\d+)-dev\.(\d+)$' -or [int]$Matches[4] -lt 2) {
        throw "Lifecycle upgrade simulation requires a dev version after dev.1; got '$Version'."
    }
    $olderDev = [int]$Matches[4] - 1
    [pscustomobject]@{
        Display = "$($Matches[1]).$($Matches[2]).$($Matches[3])-dev.$olderDev"
        Msi = "$($Matches[1]).$($Matches[2]).$(([int]$Matches[3] * 1000) + $olderDev)"
    }
}

function Get-MoonmarkEntries {
    Get-ChildItem 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall', 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall' -ErrorAction SilentlyContinue |
        Get-ItemProperty | Where-Object DisplayName -eq 'Moonmark'
}

Push-Location $projectRoot
try {
    $version = Get-Version
    if (-not $ReleaseDirectory) { $ReleaseDirectory = Join-Path $outRoot "release/$version" }
    $ReleaseDirectory = [IO.Path]::GetFullPath($ReleaseDirectory)
    $msi = Join-Path $ReleaseDirectory 'Moonmark-win-x64.msi'
    $setup = Join-Path $ReleaseDirectory 'Moonmark-Setup-win-x64.exe'
    $zip = Join-Path $ReleaseDirectory 'Moonmark-portable-win-x64.zip'
    $checksums = Join-Path $ReleaseDirectory 'SHA256SUMS.txt'
    foreach ($path in $msi, $setup, $zip, $checksums) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Release artifact is missing: $path" }
    }

    $manifest = Get-Content -LiteralPath $checksums
    foreach ($artifact in $setup, $msi, $zip) {
        $name = [IO.Path]::GetFileName($artifact)
        $expected = ($manifest | Where-Object { $_ -like "*`*$name" } | Select-Object -First 1) -replace '\s+\*.*$', ''
        $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $artifact).Hash.ToLowerInvariant()
        if (-not $expected -or $expected -ne $actual) { throw "SHA-256 validation failed for $name." }
    }

    $wix = Join-Path $outRoot 'toolchains/wix/wix.exe'
    if (-not (Test-Path -LiteralPath $wix -PathType Leaf)) { throw 'WiX was not found. Run cargo setup.' }
    $auditRoot = Join-Path $outRoot 'tests/wix-installer-audit'
    if (Test-Path -LiteralPath $auditRoot) { Remove-Item -Recurse -Force -LiteralPath $auditRoot }
    New-Item -ItemType Directory -Force $auditRoot | Out-Null
    $decompiled = Join-Path $auditRoot 'Moonmark.decompiled.wxs'
    & $wix msi decompile -acceptEula wix7 -intermediateFolder (Join-Path $auditRoot 'obj') -o $decompiled $msi
    if ($LASTEXITCODE -ne 0) { throw 'MSI decompilation audit failed.' }
    $source = Get-Content -Raw -LiteralPath $decompiled
    foreach ($required in @(
        'UpgradeCode="{48D9AFC3-ECEB-4DB2-BC50-C176246E388A}"',
        'StandardDirectory Id="ProgramFiles64Folder"',
        'MoonmarkStartMenuShortcut', 'MoonmarkDesktopShortcut',
        'Moonmark.MarkdownDocument', 'Moonmark.TextDocument',
        'Software\RegisteredApplications', 'MOONMARK_FILE_ASSOC = 1',
        'MOONMARK_DESKTOP_SHORTCUT = 1', '&quot;%1&quot;'
    )) {
        if (-not $source.Contains($required)) { throw "MSI audit did not find required authoring: $required" }
    }

    $extractRoot = Join-Path $auditRoot 'portable'
    Expand-Archive -LiteralPath $zip -DestinationPath $extractRoot
    $portableRoot = Join-Path $extractRoot 'Moonmark'
    if (-not (Test-Path -LiteralPath (Join-Path $portableRoot 'portable.flag'))) { throw 'Portable archive is missing portable.flag.' }
    $savedPath = $env:PATH
    try {
        $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
        $smoke = Start-Process -FilePath (Join-Path $portableRoot 'Moonmark.exe') -ArgumentList '--smoke-icon' -WorkingDirectory $portableRoot -Wait -PassThru -WindowStyle Hidden
        if ($smoke.ExitCode -ne 0) { throw "Portable smoke failed with exit code $($smoke.ExitCode)." }
    } finally { $env:PATH = $savedPath }

    if (-not $ExecuteLifecycle) {
        Write-Host 'WIX_INSTALLER_AUDIT artifacts=ok checksums=ok msi_tables=ok portable=ok lifecycle=not-requested'
        return
    }

    $principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Lifecycle validation requires an elevated PowerShell session because Moonmark is a per-machine Program Files installation.'
    }
    $existing = @(Get-MoonmarkEntries)
    if ($existing.Count -and -not $AllowExistingMoonmarkReplacement) {
        $details = ($existing | ForEach-Object { "$($_.DisplayVersion): $($_.UninstallString)" }) -join '; '
        throw "An existing Moonmark installation is present. Refusing to replace it without -AllowExistingMoonmarkReplacement. Found: $details"
    }

    $lifecycleRoot = Join-Path $outRoot 'tests/wix-installer-lifecycle'
    $installRoot = Join-Path $lifecycleRoot 'install'
    $logs = Join-Path $lifecycleRoot 'logs'
    $olderBuild = Join-Path $lifecycleRoot 'older'
    New-Item -ItemType Directory -Force $installRoot, $logs, $olderBuild | Out-Null
    $userDocument = Join-Path $lifecycleRoot 'User document.md'
    Set-Content -LiteralPath $userDocument -Value '# User-owned document' -Encoding utf8NoBOM
    $olderVersion = Get-OlderInstallerVersion $version
    $payloadRoot = Join-Path $outRoot "package/staging/$version/Moonmark"
    if (-not (Test-Path -LiteralPath (Join-Path $payloadRoot 'Moonmark.exe') -PathType Leaf)) {
        throw 'The staged application payload is missing. Run cargo package-app before lifecycle validation.'
    }
    $olderMsi = Join-Path $olderBuild 'Moonmark-older-win-x64.msi'
    & $wix build -acceptEula wix7 -arch x64 -pdbtype none -bindpath "Payload=$payloadRoot" `
        -d "MsiVersion=$($olderVersion.Msi)" -d "DisplayVersion=$($olderVersion.Display)" `
        -d "ProjectRoot=$projectRoot" -intermediatefolder (Join-Path $olderBuild 'obj') `
        'packaging/windows/wix/Moonmark.wxs' -o $olderMsi
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $olderMsi -PathType Leaf)) {
        throw 'Older MSI generation for upgrade simulation failed.'
    }
    try {
        Invoke-Msi @('/i', $olderMsi, "INSTALLFOLDER=$installRoot", 'MOONMARK_FILE_ASSOC=1', 'MOONMARK_DESKTOP_SHORTCUT=1') (Join-Path $logs 'older-install.log')
        $registeredVersion = (Get-ItemProperty 'HKLM:\Software\ItsW0lfiy\Moonmark\Installer').Version
        if ($registeredVersion -ne $olderVersion.Display) { throw "Older MSI registered '$registeredVersion' instead of '$($olderVersion.Display)'." }

        Invoke-Setup $setup @() (Join-Path $logs 'bundle-upgrade.log')
        $registeredVersion = (Get-ItemProperty 'HKLM:\Software\ItsW0lfiy\Moonmark\Installer').Version
        if ($registeredVersion -ne $version) { throw "Bundle upgrade registered '$registeredVersion' instead of '$version'." }
        $entries = @(Get-MoonmarkEntries)
        if ($entries.Count -ne 1) { throw "Bundle upgrade left $($entries.Count) visible Moonmark Installed Apps entries instead of one." }
        Invoke-Setup $setup @() (Join-Path $logs 'bundle-same-version.log')
        Invoke-Setup $setup @('/repair') (Join-Path $logs 'bundle-repair.log')

        if (-not (Test-Path -LiteralPath (Join-Path $installRoot 'Moonmark.exe'))) { throw 'Bundle upgrade did not deploy Moonmark.exe.' }
        $launch = Start-Process -FilePath (Join-Path $installRoot 'Moonmark.exe') -ArgumentList @('--smoke-startup-arguments', $userDocument) -Wait -PassThru -WindowStyle Hidden
        if ($launch.ExitCode -ne 0) { throw "Installed launch smoke failed with exit code $($launch.ExitCode)." }

        Invoke-Setup $setup @('/uninstall') (Join-Path $logs 'bundle-uninstall.log')
        if (Test-Path -LiteralPath $installRoot) { throw 'Bundle uninstall left the isolated install directory.' }
        if (@(Get-MoonmarkEntries).Count -ne 0) { throw 'Bundle uninstall left a visible Moonmark Installed Apps entry.' }

        Invoke-Msi @('/i', $msi, "INSTALLFOLDER=$installRoot", 'MOONMARK_FILE_ASSOC=1', 'MOONMARK_DESKTOP_SHORTCUT=1') (Join-Path $logs 'msi-install.log')
        if (-not (Test-Path -LiteralPath (Join-Path $installRoot 'Moonmark.exe'))) { throw 'Direct MSI install did not deploy Moonmark.exe.' }

        Remove-Item -Force -LiteralPath (Join-Path $installRoot 'Moonmark.exe')
        Invoke-Msi @('/fa', $msi) (Join-Path $logs 'msi-repair.log')
        if (-not (Test-Path -LiteralPath (Join-Path $installRoot 'Moonmark.exe'))) { throw 'Direct MSI repair did not restore Moonmark.exe.' }

        Invoke-Msi @('/i', $msi, "INSTALLFOLDER=$installRoot", 'MOONMARK_FILE_ASSOC=0', 'MOONMARK_DESKTOP_SHORTCUT=0') (Join-Path $logs 'msi-modify.log')
        Invoke-Msi @('/x', $msi) (Join-Path $logs 'msi-uninstall.log')
        if (Test-Path -LiteralPath $installRoot) { throw 'Direct MSI uninstall left the isolated install directory.' }
        if (-not (Test-Path -LiteralPath $userDocument)) { throw 'Uninstall removed a user-owned document.' }
        Write-Host 'WIX_INSTALLER_LIFECYCLE older_install=ok bundle_upgrade=ok same_version=ok bundle_repair=ok bundle_uninstall=ok msi_install=ok msi_repair=ok msi_modify=ok msi_uninstall=ok installed_apps=unique user_document=preserved'
    } finally {
        if (Test-Path -LiteralPath (Join-Path $installRoot 'Moonmark.exe')) {
            try { Invoke-Setup $setup @('/uninstall') (Join-Path $logs 'cleanup-bundle-uninstall.log') } catch { Write-Warning $_ }
            if (Test-Path -LiteralPath (Join-Path $installRoot 'Moonmark.exe')) {
                try { Invoke-Msi @('/x', $msi) (Join-Path $logs 'cleanup-msi-uninstall.log') } catch { Write-Warning $_ }
                try { Invoke-Msi @('/x', $olderMsi) (Join-Path $logs 'cleanup-older-msi-uninstall.log') } catch { Write-Warning $_ }
            }
        }
    }
} finally {
    Pop-Location
}
