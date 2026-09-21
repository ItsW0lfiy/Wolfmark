[CmdletBinding()]
param(
    [string]$ReleaseDirectory,
    [switch]$ExecuteLifecycle,
    [switch]$AllowExistingWolfmarkReplacement,
    [switch]$InteractiveLaunchSmoke
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$outRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'out'))

function Get-Version {
    $metadata = (& cargo metadata --format-version 1 --no-deps) | ConvertFrom-Json
    if ($LASTEXITCODE -ne 0) { throw 'cargo metadata failed.' }
    return ($metadata.packages | Where-Object name -eq 'wolfmark' | Select-Object -First 1).version
}

function Get-MsiProperty([string]$MsiPath, [string]$Name) {
    $installer = New-Object -ComObject WindowsInstaller.Installer
    $database = $installer.GetType().InvokeMember(
        'OpenDatabase', 'InvokeMethod', $null, $installer, @([IO.Path]::GetFullPath($MsiPath), 0))
    $query = "SELECT ``Value`` FROM ``Property`` WHERE ``Property``='$Name'"
    $view = $database.GetType().InvokeMember('OpenView', 'InvokeMethod', $null, $database, @($query))
    $view.GetType().InvokeMember('Execute', 'InvokeMethod', $null, $view, $null) | Out-Null
    $record = $view.GetType().InvokeMember('Fetch', 'InvokeMethod', $null, $view, $null)
    if (-not $record) { return $null }
    return $record.GetType().InvokeMember('StringData', 'GetProperty', $null, $record, 1)
}

function Get-MsiPackageCode([string]$MsiPath) {
    $installer = New-Object -ComObject WindowsInstaller.Installer
    $database = $installer.GetType().InvokeMember(
        'OpenDatabase', 'InvokeMethod', $null, $installer, @([IO.Path]::GetFullPath($MsiPath), 0))
    $summary = $database.GetType().InvokeMember('SummaryInformation', 'GetProperty', $null, $database, 0)
    return $summary.GetType().InvokeMember('Property', 'GetProperty', $null, $summary, 9)
}

function Invoke-Msi([string[]]$Arguments, [string]$LogPath) {
    $process = Start-Process -FilePath "$env:SystemRoot\System32\msiexec.exe" -ArgumentList ($Arguments + @('/qn', '/norestart', '/L*v', $LogPath)) -Wait -PassThru
    if ($process.ExitCode -notin 0, 3010) { throw "Windows Installer failed with exit code $($process.ExitCode). See $LogPath" }
}

function Invoke-Setup([string]$SetupPath, [string[]]$Arguments, [string]$LogPath) {
    $process = Start-Process -FilePath $SetupPath -ArgumentList ($Arguments + @('/quiet', '/norestart', '/log', $LogPath)) -Wait -PassThru
    if ($process.ExitCode -notin 0, 3010) { throw "Wolfmark Setup failed with exit code $($process.ExitCode). See $LogPath" }
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

function Get-WolfmarkEntries {
    Get-ChildItem 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall', 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall' -ErrorAction SilentlyContinue |
        Get-ItemProperty | Where-Object DisplayName -eq 'Wolfmark'
}

function Wait-VisibleWindow([string]$ProcessName, [int]$TimeoutSeconds = 15) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $window = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue |
            Where-Object { $_.MainWindowHandle -ne 0 -and $_.MainWindowTitle -eq 'Wolfmark Setup' } |
            Select-Object -First 1
        if ($window) { return $window }
        Start-Sleep -Milliseconds 200
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Timed out waiting for the packaged $ProcessName Wolfmark Setup window."
}

function Invoke-InteractiveLaunchSmoke([string]$SetupPath, [string]$MsiPath) {
    $bundle = $null
    $bundleUi = $null
    try {
        $bundle = Start-Process -FilePath $SetupPath -PassThru
        $bundleUi = Wait-VisibleWindow 'WolfmarkSetup'
        if (-not $bundleUi.CloseMainWindow()) { throw 'The Burn setup window rejected a normal close request.' }
        if (-not $bundleUi.WaitForExit(15000)) { throw 'The Burn setup UI did not exit after cancellation.' }
        if ($bundle -and -not $bundle.HasExited -and -not $bundle.WaitForExit(15000)) {
            throw 'The Burn engine remained alive after its setup UI closed.'
        }
    } finally {
        if ($bundleUi -and -not $bundleUi.HasExited) { Stop-Process -Id $bundleUi.Id -Force }
        if ($bundle -and -not $bundle.HasExited) { Stop-Process -Id $bundle.Id -Force }
    }

    $msiClient = $null
    try {
        $msiClient = Start-Process -FilePath "$env:SystemRoot\System32\msiexec.exe" -ArgumentList @('/i', "`"$MsiPath`"") -PassThru
        $msiUi = Wait-VisibleWindow 'msiexec'
        if (-not $msiUi.CloseMainWindow()) { throw 'The MSI Embedded UI rejected a normal close request.' }
        if (-not $msiUi.WaitForExit(15000)) { throw 'The MSI Embedded UI did not exit after cancellation.' }
    } finally {
        if ($msiClient -and -not $msiClient.HasExited) { Stop-Process -Id $msiClient.Id -Force }
    }
    Write-Host 'WIX_INTERACTIVE_LAUNCH_SMOKE burn_visible=ok burn_cancel_cleanup=ok msi_embedded_ui_visible=ok msi_cancel_cleanup=ok'
}

function Invoke-QuietLayoutSmoke([string]$SetupPath, [string]$AuditRoot) {
    $layoutRoot = Join-Path $AuditRoot 'quiet-layout'
    $logPath = Join-Path $AuditRoot 'quiet-layout.log'
    if (Test-Path -LiteralPath $layoutRoot) {
        Remove-Item -Recurse -Force -LiteralPath $layoutRoot
    }
    New-Item -ItemType Directory -Force $layoutRoot | Out-Null
    $existingUiProcesses = @(Get-Process -Name 'WolfmarkSetup' -ErrorAction SilentlyContinue).Id
    $quotedLayoutRoot = [char]34 + $layoutRoot + [char]34
    $quotedLogPath = [char]34 + $logPath + [char]34
    $process = Start-Process -FilePath $SetupPath -ArgumentList @(
        '/layout', $quotedLayoutRoot, '/quiet', '/norestart', '/log', $quotedLogPath
    ) -PassThru
    try {
        $deadline = [DateTime]::UtcNow.AddSeconds(30)
        while (-not $process.WaitForExit(100)) {
            $visibleUi = Get-Process -Name 'WolfmarkSetup' -ErrorAction SilentlyContinue |
                Where-Object {
                    $_.Id -notin $existingUiProcesses -and $_.MainWindowHandle -ne 0
                } |
                Select-Object -First 1
            if ($visibleUi) {
                throw 'Quiet Burn layout unexpectedly created a visible Wolfmark setup window.'
            }
            if ([DateTime]::UtcNow -ge $deadline) {
                throw 'Quiet Burn layout did not finish within 30 seconds.'
            }
        }
        if ($process.ExitCode -notin 0, 3010) {
            throw "Quiet Burn layout failed with exit code $($process.ExitCode). See $logPath"
        }
    } finally {
        if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force }
    }
    $laidOutBundle = Join-Path $layoutRoot ([IO.Path]::GetFileName($SetupPath))
    if (-not (Test-Path -LiteralPath $laidOutBundle -PathType Leaf)) {
        throw 'Quiet Burn layout completed without producing its bundle payload.'
    }
    if ((Get-FileHash -Algorithm SHA256 -LiteralPath $laidOutBundle).Hash -ne
        (Get-FileHash -Algorithm SHA256 -LiteralPath $SetupPath).Hash) {
        throw 'Quiet Burn layout produced bundle bytes that differ from the release artifact.'
    }
    Write-Host 'WIX_QUIET_LAYOUT_SMOKE apply=ok visible_ui=none'
}

Push-Location $projectRoot
try {
    $version = Get-Version
    $identity = Get-Content -Raw -LiteralPath 'packaging/windows/identity.json' | ConvertFrom-Json
    if ($identity.displayVersion -ne $version) {
        throw "Installer identity is for '$($identity.displayVersion)', not '$version'."
    }
    if (-not $ReleaseDirectory) { $ReleaseDirectory = Join-Path $outRoot "release/$version" }
    $ReleaseDirectory = [IO.Path]::GetFullPath($ReleaseDirectory)
    $msi = Join-Path $ReleaseDirectory 'Wolfmark-win-x64.msi'
    $setup = Join-Path $ReleaseDirectory 'Wolfmark-Setup-win-x64.exe'
    $zip = Join-Path $ReleaseDirectory 'Wolfmark-portable-win-x64.zip'
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
    $decompiled = Join-Path $auditRoot 'Wolfmark.decompiled.wxs'
    & $wix msi decompile -acceptEula wix7 -intermediateFolder (Join-Path $auditRoot 'obj') -o $decompiled $msi
    if ($LASTEXITCODE -ne 0) { throw 'MSI decompilation audit failed.' }
    $source = Get-Content -Raw -LiteralPath $decompiled
    foreach ($required in @(
        "ProductCode=`"$($identity.msiProductCode)`"",
        "UpgradeCode=`"$($identity.msiUpgradeCode)`"",
        'StandardDirectory Id="ProgramFiles64Folder"',
        'WolfmarkStartMenuShortcut', 'WolfmarkDesktopShortcut',
        'Wolfmark.MarkdownDocument', 'Wolfmark.TextDocument',
        'Software\RegisteredApplications', 'WOLFMARK_FILE_ASSOC = 1',
        'WOLFMARK_DESKTOP_SHORTCUT = 1', '&quot;%1&quot;',
        'EmbeddedUI Id="WolfmarkEmbeddedUI"', 'WolfmarkMsiEmbeddedUI.dll',
        'WolfmarkMsiUi.dll', 'qwindows.dll'
    )) {
        if (-not $source.Contains($required)) { throw "MSI audit did not find required authoring: $required" }
    }
    if ((Get-MsiProperty $msi 'ProductCode') -ne $identity.msiProductCode) {
        throw 'The release MSI ProductCode does not match the versioned identity manifest.'
    }

    $bundleAudit = Join-Path $auditRoot 'bundle'
    New-Item -ItemType Directory -Force (Join-Path $bundleAudit 'obj'), (Join-Path $bundleAudit 'payload'), (Join-Path $bundleAudit 'ba') | Out-Null
    & $wix burn extract -acceptEula wix7 -intermediateFolder (Join-Path $bundleAudit 'obj') `
        -o (Join-Path $bundleAudit 'payload') -oba (Join-Path $bundleAudit 'ba') $setup
    if ($LASTEXITCODE -ne 0) { throw 'Bundle identity extraction failed.' }
    $bundleManifest = Get-Content -Raw -LiteralPath (Join-Path $bundleAudit 'ba/manifest.xml')
    foreach ($required in @(
        "ProviderKey=`"$($identity.bundleProviderKey)`"",
        "PrimaryUpgradeCode=`"$($identity.bundleUpgradeCode)`"",
        "ProductCode=`"$($identity.msiProductCode)`""
    )) {
        if (-not $bundleManifest.Contains($required)) { throw "Bundle audit did not find required identity: $required" }
    }

    $identityProbeMsi = Join-Path $auditRoot 'Wolfmark-identity-rebuild.msi'
    $identityProbeObj = Join-Path $auditRoot 'identity-rebuild-obj'
    $payloadRoot = Join-Path $outRoot "package/staging/$version/Wolfmark"
    $embeddedUiRoot = Join-Path $outRoot 'package/wix/msi-ui-payload'
    $msiVersion = Get-MsiProperty $msi 'ProductVersion'
    & $wix build -acceptEula wix7 -arch x64 -pdbtype none -bindpath "Payload=$payloadRoot" -bindpath "EmbeddedUI=$embeddedUiRoot" `
        -d "MsiVersion=$msiVersion" -d "DisplayVersion=$version" `
        -d "MsiProductCode=$($identity.msiProductCode)" -d "MsiUpgradeCode=$($identity.msiUpgradeCode)" `
        -d "ProjectRoot=$projectRoot" -intermediatefolder $identityProbeObj `
        'packaging/windows/wix/Wolfmark.wxs' -o $identityProbeMsi
    if ($LASTEXITCODE -ne 0) { throw 'Same-version identity rebuild failed.' }
    if ((Get-MsiProperty $identityProbeMsi 'ProductCode') -ne (Get-MsiProperty $msi 'ProductCode')) {
        throw 'A same-version rebuild changed the MSI ProductCode.'
    }
    if ((Get-MsiPackageCode $identityProbeMsi) -eq (Get-MsiPackageCode $msi)) {
        throw 'A same-version rebuilt MSI reused the PackageCode.'
    }
    Write-Host (
        'WIX_INSTALLER_IDENTITY product_code={0} package_code={1} upgrade_code={2} bundle_upgrade_code={3} provider_key={4} same_version_rebuild=stable' -f
        (Get-MsiProperty $msi 'ProductCode'), (Get-MsiPackageCode $msi),
        (Get-MsiProperty $msi 'UpgradeCode'), $identity.bundleUpgradeCode, $identity.bundleProviderKey)

    $extractRoot = Join-Path $auditRoot 'portable'
    Expand-Archive -LiteralPath $zip -DestinationPath $extractRoot
    $portableRoot = Join-Path $extractRoot 'Wolfmark'
    if (-not (Test-Path -LiteralPath (Join-Path $portableRoot 'portable.flag'))) { throw 'Portable archive is missing portable.flag.' }
    $savedPath = $env:PATH
    try {
        $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
        $smoke = Start-Process -FilePath (Join-Path $portableRoot 'Wolfmark.exe') -ArgumentList '--smoke-icon' -WorkingDirectory $portableRoot -Wait -PassThru -WindowStyle Hidden
        if ($smoke.ExitCode -ne 0) { throw "Portable smoke failed with exit code $($smoke.ExitCode)." }
    } finally { $env:PATH = $savedPath }

    Invoke-QuietLayoutSmoke $setup $auditRoot

    if ($InteractiveLaunchSmoke) {
        Invoke-InteractiveLaunchSmoke $setup $msi
    }

    if (-not $ExecuteLifecycle) {
        Write-Host 'WIX_INSTALLER_AUDIT artifacts=ok checksums=ok msi_tables=ok portable=ok lifecycle=not-requested'
        return
    }

    $principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Lifecycle validation requires an elevated PowerShell session because Wolfmark is a per-machine Program Files installation.'
    }
    $existing = @(Get-WolfmarkEntries)
    if ($existing.Count -and -not $AllowExistingWolfmarkReplacement) {
        $details = ($existing | ForEach-Object { "$($_.DisplayVersion): $($_.UninstallString)" }) -join '; '
        throw "An existing Wolfmark installation is present. Refusing to replace it without -AllowExistingWolfmarkReplacement. Found: $details"
    }

    $lifecycleRoot = Join-Path $outRoot 'tests/wix-installer-lifecycle'
    $installRoot = Join-Path $lifecycleRoot 'install'
    $logs = Join-Path $lifecycleRoot 'logs'
    $olderBuild = Join-Path $lifecycleRoot 'older'
    New-Item -ItemType Directory -Force $installRoot, $logs, $olderBuild | Out-Null
    $userDocument = Join-Path $lifecycleRoot 'User document.md'
    Set-Content -LiteralPath $userDocument -Value '# User-owned document' -Encoding utf8NoBOM
    $olderVersion = Get-OlderInstallerVersion $version
    $payloadRoot = Join-Path $outRoot "package/staging/$version/Wolfmark"
    if (-not (Test-Path -LiteralPath (Join-Path $payloadRoot 'Wolfmark.exe') -PathType Leaf)) {
        throw 'The staged application payload is missing. Run cargo package-app before lifecycle validation.'
    }
    $olderMsi = Join-Path $olderBuild 'Wolfmark-older-win-x64.msi'
    $embeddedUiRoot = Join-Path $outRoot 'package/wix/msi-ui-payload'
    & $wix build -acceptEula wix7 -arch x64 -pdbtype none -bindpath "Payload=$payloadRoot" -bindpath "EmbeddedUI=$embeddedUiRoot" `
        -d "MsiVersion=$($olderVersion.Msi)" -d "DisplayVersion=$($olderVersion.Display)" `
        -d "MsiProductCode={1A2A7C5B-F554-4132-983B-90FE7E474686}" -d "MsiUpgradeCode=$($identity.msiUpgradeCode)" `
        -d "ProjectRoot=$projectRoot" -intermediatefolder (Join-Path $olderBuild 'obj') `
        'packaging/windows/wix/Wolfmark.wxs' -o $olderMsi
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $olderMsi -PathType Leaf)) {
        throw 'Older MSI generation for upgrade simulation failed.'
    }
    try {
        Invoke-Msi @('/i', $olderMsi, "INSTALLFOLDER=$installRoot", 'WOLFMARK_FILE_ASSOC=1', 'WOLFMARK_DESKTOP_SHORTCUT=1') (Join-Path $logs 'older-install.log')
        $registeredVersion = (Get-ItemProperty 'HKLM:\Software\ItsW0lfiy\Wolfmark\Installer').Version
        if ($registeredVersion -ne $olderVersion.Display) { throw "Older MSI registered '$registeredVersion' instead of '$($olderVersion.Display)'." }

        Invoke-Setup $setup @() (Join-Path $logs 'bundle-upgrade.log')
        $registeredVersion = (Get-ItemProperty 'HKLM:\Software\ItsW0lfiy\Wolfmark\Installer').Version
        if ($registeredVersion -ne $version) { throw "Bundle upgrade registered '$registeredVersion' instead of '$version'." }
        $entries = @(Get-WolfmarkEntries)
        if ($entries.Count -ne 1) { throw "Bundle upgrade left $($entries.Count) visible Wolfmark Installed Apps entries instead of one." }
        Invoke-Setup $setup @() (Join-Path $logs 'bundle-same-version.log')
        Invoke-Setup $setup @('/repair') (Join-Path $logs 'bundle-repair.log')

        if (-not (Test-Path -LiteralPath (Join-Path $installRoot 'Wolfmark.exe'))) { throw 'Bundle upgrade did not deploy Wolfmark.exe.' }
        $launch = Start-Process -FilePath (Join-Path $installRoot 'Wolfmark.exe') -ArgumentList @('--smoke-startup-arguments', $userDocument) -Wait -PassThru -WindowStyle Hidden
        if ($launch.ExitCode -ne 0) { throw "Installed launch smoke failed with exit code $($launch.ExitCode)." }

        Invoke-Setup $setup @('/uninstall') (Join-Path $logs 'bundle-uninstall.log')
        if (Test-Path -LiteralPath $installRoot) { throw 'Bundle uninstall left the isolated install directory.' }
        if (@(Get-WolfmarkEntries).Count -ne 0) { throw 'Bundle uninstall left a visible Wolfmark Installed Apps entry.' }

        Invoke-Msi @('/i', $msi, "INSTALLFOLDER=$installRoot", 'WOLFMARK_FILE_ASSOC=1', 'WOLFMARK_DESKTOP_SHORTCUT=1') (Join-Path $logs 'msi-install.log')
        if (-not (Test-Path -LiteralPath (Join-Path $installRoot 'Wolfmark.exe'))) { throw 'Direct MSI install did not deploy Wolfmark.exe.' }

        Remove-Item -Force -LiteralPath (Join-Path $installRoot 'Wolfmark.exe')
        Invoke-Msi @('/fa', $msi) (Join-Path $logs 'msi-repair.log')
        if (-not (Test-Path -LiteralPath (Join-Path $installRoot 'Wolfmark.exe'))) { throw 'Direct MSI repair did not restore Wolfmark.exe.' }

        Invoke-Msi @('/i', $msi, "INSTALLFOLDER=$installRoot", 'WOLFMARK_FILE_ASSOC=0', 'WOLFMARK_DESKTOP_SHORTCUT=0') (Join-Path $logs 'msi-modify.log')
        Invoke-Msi @('/x', $msi) (Join-Path $logs 'msi-uninstall.log')
        if (Test-Path -LiteralPath $installRoot) { throw 'Direct MSI uninstall left the isolated install directory.' }
        if (-not (Test-Path -LiteralPath $userDocument)) { throw 'Uninstall removed a user-owned document.' }
        Write-Host 'WIX_INSTALLER_LIFECYCLE older_install=ok bundle_upgrade=ok same_version=ok bundle_repair=ok bundle_uninstall=ok msi_install=ok msi_repair=ok msi_modify=ok msi_uninstall=ok installed_apps=unique user_document=preserved'
    } finally {
        if (Test-Path -LiteralPath (Join-Path $installRoot 'Wolfmark.exe')) {
            try { Invoke-Setup $setup @('/uninstall') (Join-Path $logs 'cleanup-bundle-uninstall.log') } catch { Write-Warning $_ }
            if (Test-Path -LiteralPath (Join-Path $installRoot 'Wolfmark.exe')) {
                try { Invoke-Msi @('/x', $msi) (Join-Path $logs 'cleanup-msi-uninstall.log') } catch { Write-Warning $_ }
                try { Invoke-Msi @('/x', $olderMsi) (Join-Path $logs 'cleanup-older-msi-uninstall.log') } catch { Write-Warning $_ }
            }
        }
    }
} finally {
    Pop-Location
}
