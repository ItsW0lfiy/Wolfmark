[CmdletBinding()]
param(
    [string]$SetupPath,
    [string]$InnoCompiler
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$testRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'out/tests/installer'))
$outRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'out'))
$outPrefix = $outRoot.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
if (-not $testRoot.StartsWith($outPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to use installer test path outside out: $testRoot"
}

function Get-MoonmarkVersion {
    $metadata = (& cargo metadata --format-version 1 --no-deps | ConvertFrom-Json)
    if ($LASTEXITCODE -ne 0) { throw 'cargo metadata failed.' }
    $manifestPath = [IO.Path]::GetFullPath((Join-Path $projectRoot 'Cargo.toml'))
    return ($metadata.packages | Where-Object {
        [IO.Path]::GetFullPath($_.manifest_path) -eq $manifestPath
    } | Select-Object -First 1).version
}

function Find-InnoCompiler {
    $candidates = @($InnoCompiler, $env:MOONMARK_INNO_ISCC)
    $candidates += Get-ChildItem -LiteralPath (Join-Path $projectRoot 'out/toolchains/inno') `
        -Filter ISCC.exe -File -Recurse -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending | Select-Object -ExpandProperty FullName
    $selected = $candidates | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) } |
        Select-Object -First 1
    if (-not $selected) { throw 'Inno Setup compiler not found. Run scripts/bootstrap_inno.ps1.' }
    return [IO.Path]::GetFullPath($selected)
}

function Invoke-Installer([string]$Path, [string]$InstallRoot, [string]$LogPath) {
    $arguments = @(
        '/SP-', '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/CURRENTUSER',
        "/DIR=$InstallRoot", '/TASKS=desktopicon,fileassoc', "/LOG=$LogPath"
    )
    $process = Start-Process -FilePath $Path -ArgumentList $arguments `
        -Wait -PassThru -WindowStyle Hidden
    if ($process.ExitCode -ne 0) {
        throw "Installer failed with exit code $($process.ExitCode). See $LogPath"
    }
}

function Invoke-Uninstaller([string]$Path, [string]$LogPath) {
    $arguments = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=$LogPath")
    $process = Start-Process -FilePath $Path -ArgumentList $arguments `
        -Wait -PassThru -WindowStyle Hidden
    if ($process.ExitCode -ne 0) {
        throw "Uninstaller failed with exit code $($process.ExitCode). See $LogPath"
    }
}

function Wait-Until([scriptblock]$Condition) {
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        if (& $Condition) { return $true }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    return $false
}

function Get-MoonmarkUninstallEntries {
    $root = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall'
    if (-not (Test-Path $root)) { return @() }
    return @(Get-ChildItem $root | Get-ItemProperty | Where-Object DisplayName -eq 'Moonmark')
}

function Get-UserChoice([string]$Extension) {
    $path = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\$Extension\UserChoice"
    if (-not (Test-Path $path)) { return $null }
    return (Get-ItemProperty -Path $path -Name ProgId -ErrorAction SilentlyContinue).ProgId
}

function Assert-RegistryValue([string]$Path, [string]$Name, [object]$Expected) {
    if (-not (Test-Path $Path)) { throw "Expected registry key is missing: $Path" }
    $actual = (Get-Item -Path $Path).GetValue($Name, $null)
    if ($actual -ne $Expected) {
        throw "Registry value mismatch at $Path [$Name]. Expected '$Expected', got '$actual'."
    }
}

Push-Location $projectRoot
$installRoot = Join-Path $testRoot 'installed/Moonmark'
$documentsRoot = Join-Path $testRoot 'documents/Deep Folder/More Documents'
$startMenuShortcut = Join-Path $env:APPDATA 'Microsoft/Windows/Start Menu/Programs/Moonmark/Moonmark.lnk'
$desktopShortcut = Join-Path ([Environment]::GetFolderPath('Desktop')) 'Moonmark.lnk'
$uninstaller = Join-Path $installRoot 'unins000.exe'
try {
    if (Get-MoonmarkUninstallEntries) {
        throw 'A current-user Moonmark installation already exists; refusing to overwrite it during tests.'
    }
    if ((Test-Path -LiteralPath $startMenuShortcut) -or
        (Test-Path -LiteralPath $desktopShortcut)) {
        throw 'A Moonmark shortcut already exists; refusing to overwrite it during tests.'
    }
    if (Test-Path -LiteralPath $testRoot) {
        Remove-Item -Recurse -Force -LiteralPath $testRoot
    }
    New-Item -ItemType Directory -Force -Path $documentsRoot | Out-Null

    $version = Get-MoonmarkVersion
    if (-not $SetupPath) {
        $SetupPath = Join-Path $projectRoot "out/release/$version/Moonmark-Setup-win-x64.exe"
    }
    $SetupPath = (Resolve-Path -LiteralPath $SetupPath).Path
    $compiler = Find-InnoCompiler
    $sourceDir = [IO.Path]::GetFullPath((Join-Path $projectRoot "out/package/staging/$version/Moonmark"))
    $olderOutput = Join-Path $testRoot 'older'
    New-Item -ItemType Directory -Force -Path $olderOutput | Out-Null
    & $compiler '/Qp' '/DMyAppVersion=0.1.0-dev.7-preupgrade' '/DMyNumericVersion=0.1.0.6' `
        "/DSourceDir=$sourceDir" "/DOutputDir=$olderOutput" "/DProjectRoot=$projectRoot" `
        (Join-Path $projectRoot 'packaging/windows/Moonmark.iss')
    if ($LASTEXITCODE -ne 0) { throw 'Older installer fixture compilation failed.' }
    $olderSetup = Join-Path $olderOutput 'Moonmark-Setup-win-x64.exe'

    $documents = @(
        (Join-Path $documentsRoot 'README.md'),
        (Join-Path $documentsRoot 'My Document.markdown'),
        (Join-Path $documentsRoot '日本語 document.txt')
    )
    Set-Content -LiteralPath $documents[0] -Value '# Installer Markdown test' -Encoding utf8NoBOM
    Set-Content -LiteralPath $documents[1] -Value '# Installer spaced-path test' -Encoding utf8NoBOM
    Set-Content -LiteralPath $documents[2] -Value 'Installer Unicode path test' -Encoding utf8NoBOM

    $choiceBefore = @{}
    foreach ($extension in '.md', '.markdown', '.txt') {
        $choiceBefore[$extension] = Get-UserChoice $extension
    }

    Invoke-Installer $olderSetup $installRoot (Join-Path $testRoot 'install-older.log')
    $entries = Get-MoonmarkUninstallEntries
    if ($entries.Count -ne 1 -or $entries[0].DisplayVersion -ne '0.1.0-dev.7-preupgrade') {
        throw 'Older installer did not create exactly one expected Installed Apps entry.'
    }

    Invoke-Installer $SetupPath $installRoot (Join-Path $testRoot 'upgrade.log')
    Invoke-Installer $SetupPath $installRoot (Join-Path $testRoot 'reinstall.log')
    $entries = Get-MoonmarkUninstallEntries
    if ($entries.Count -ne 1 -or $entries[0].DisplayVersion -ne $version) {
        throw 'Upgrade/reinstall did not preserve one current Installed Apps entry.'
    }

    foreach ($relative in @(
        'Moonmark.exe', 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6Network.dll',
        'platforms/qwindows.dll', 'qt.conf', 'LICENSE', 'THIRD_PARTY_NOTICES.txt',
        'assets/icons/moonmark-markdown.ico', 'assets/icons/moonmark-text.ico',
        'licenses/Qt-LGPL-3.0-only.txt', 'licenses/Qt-GPL-3.0-only.txt'
    )) {
        if (-not (Test-Path -LiteralPath (Join-Path $installRoot $relative) -PathType Leaf)) {
            throw "Installed payload is missing $relative"
        }
    }
    if (-not (Test-Path -LiteralPath $startMenuShortcut -PathType Leaf) -or
        -not (Test-Path -LiteralPath $desktopShortcut -PathType Leaf)) {
        throw 'Expected Start Menu or optional Desktop shortcut is missing.'
    }
    $shell = New-Object -ComObject WScript.Shell
    foreach ($shortcutPath in $startMenuShortcut, $desktopShortcut) {
        $shortcut = $shell.CreateShortcut($shortcutPath)
        if ([IO.Path]::GetFullPath($shortcut.TargetPath) -ne [IO.Path]::GetFullPath((Join-Path $installRoot 'Moonmark.exe'))) {
            throw "Shortcut target is incorrect: $shortcutPath"
        }
    }

    Assert-RegistryValue 'HKCU:\Software\RegisteredApplications' 'Moonmark' 'Software\ItsW0lfiy\Moonmark\Capabilities'
    Assert-RegistryValue 'HKCU:\Software\ItsW0lfiy\Moonmark\Capabilities\FileAssociations' '.md' 'Moonmark.MarkdownDocument'
    Assert-RegistryValue 'HKCU:\Software\ItsW0lfiy\Moonmark\Capabilities\FileAssociations' '.markdown' 'Moonmark.MarkdownDocument'
    Assert-RegistryValue 'HKCU:\Software\ItsW0lfiy\Moonmark\Capabilities\FileAssociations' '.txt' 'Moonmark.TextDocument'
    $expectedCommand = '"' + (Join-Path $installRoot 'Moonmark.exe') + '" "%1"'
    $expectedMarkdownIcon = Join-Path $installRoot 'assets/icons/moonmark-markdown.ico'
    $expectedTextIcon = Join-Path $installRoot 'assets/icons/moonmark-text.ico'
    Assert-RegistryValue 'HKCU:\Software\Classes\Moonmark.MarkdownDocument\shell\open\command' '' $expectedCommand
    Assert-RegistryValue 'HKCU:\Software\Classes\Moonmark.MarkdownDocument\DefaultIcon' '' $expectedMarkdownIcon
    Assert-RegistryValue 'HKCU:\Software\Classes\Moonmark.TextDocument\shell\open\command' '' $expectedCommand
    Assert-RegistryValue 'HKCU:\Software\Classes\Moonmark.TextDocument\DefaultIcon' '' $expectedTextIcon
    if (Test-Path 'HKCU:\Software\Classes\Moonmark.Document') {
        throw 'Upgrade left the obsolete shared Moonmark.Document ProgID.'
    }
    foreach ($extension in '.md', '.markdown') {
        Assert-RegistryValue "HKCU:\Software\Classes\$extension\OpenWithProgids" 'Moonmark.MarkdownDocument' ''
        if ((Get-UserChoice $extension) -ne $choiceBefore[$extension]) {
            throw "Installer changed the protected Windows default for $extension."
        }
    }
    Assert-RegistryValue 'HKCU:\Software\Classes\.txt\OpenWithProgids' 'Moonmark.TextDocument' ''
    if ((Get-UserChoice '.txt') -ne $choiceBefore['.txt']) {
        throw 'Installer changed the protected Windows default for .txt.'
    }

    $launchArguments = '--smoke-startup-arguments ' + (($documents | ForEach-Object { '"' + $_ + '"' }) -join ' ')
    $launch = Start-Process -FilePath (Join-Path $installRoot 'Moonmark.exe') `
        -ArgumentList $launchArguments `
        -WorkingDirectory $testRoot -Wait -PassThru -WindowStyle Hidden
    if ($launch.ExitCode -ne 0) {
        throw "Installed Moonmark shell-argument smoke failed with exit code $($launch.ExitCode)."
    }

    Invoke-Uninstaller $uninstaller (Join-Path $testRoot 'uninstall.log')
    $uninstallComplete = Wait-Until {
        -not (Test-Path -LiteralPath $installRoot) -and
        -not (Test-Path -LiteralPath $startMenuShortcut) -and
        -not (Test-Path -LiteralPath $desktopShortcut) -and
        -not (Get-MoonmarkUninstallEntries)
    }
    if (-not $uninstallComplete) {
        $residue = @()
        if (Test-Path -LiteralPath $installRoot) {
            $remainingFiles = @(Get-ChildItem -LiteralPath $installRoot -Recurse -Force |
                ForEach-Object { $_.FullName.Substring($installRoot.Length).TrimStart('\') })
            $residue += "installation directory [$($remainingFiles -join ', ')]"
        }
        if (Test-Path -LiteralPath $startMenuShortcut) { $residue += 'Start Menu shortcut' }
        if (Test-Path -LiteralPath $desktopShortcut) { $residue += 'Desktop shortcut' }
        if (Get-MoonmarkUninstallEntries) { $residue += 'Installed Apps entry' }
        throw "Uninstall residue after 15 seconds: $($residue -join ', ')."
    }
    foreach ($path in @(
        'HKCU:\Software\ItsW0lfiy\Moonmark\Capabilities',
        'HKCU:\Software\Classes\Moonmark.MarkdownDocument',
        'HKCU:\Software\Classes\Moonmark.TextDocument',
        'HKCU:\Software\Classes\Applications\Moonmark.exe'
    )) {
        if (Test-Path $path) { throw "Uninstall left Moonmark shell registration: $path" }
    }
    foreach ($document in $documents) {
        if (-not (Test-Path -LiteralPath $document -PathType Leaf)) {
            throw "Uninstall removed user-owned test document: $document"
        }
    }

    Write-Host 'INSTALLER_TEST clean=ok upgrade=ok reinstall=ok installed_launch=ok shell_registry=ok shortcuts=ok uninstall=ok user_documents=preserved'
} finally {
    if (Test-Path -LiteralPath $uninstaller -PathType Leaf) {
        try { Invoke-Uninstaller $uninstaller (Join-Path $testRoot 'cleanup-uninstall.log') } catch { Write-Warning $_ }
    }
    if (Test-Path -LiteralPath $testRoot) {
        Remove-Item -Recurse -Force -LiteralPath $testRoot
    }
    Pop-Location
}
