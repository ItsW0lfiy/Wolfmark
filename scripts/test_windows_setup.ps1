[CmdletBinding()]
param(
    [string]$ReleaseDirectory,
    [switch]$ExecuteLifecycle,
    [switch]$AllowExistingWolfmarkReplacement
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$outRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'out'))

function Assert-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'The lifecycle matrix requires an elevated PowerShell session.'
    }
}

function Invoke-Setup([string]$Setup, [string[]]$Arguments) {
    $process = Start-Process -FilePath $Setup -ArgumentList $Arguments -Wait -PassThru
    if ($process.ExitCode -ne 0) {
        throw "Setup failed with exit code $($process.ExitCode): $($Arguments -join ' ')"
    }
}

Push-Location $projectRoot
try {
    $version = ((& cargo metadata --format-version 1 --no-deps) | ConvertFrom-Json).packages |
        Where-Object name -eq 'wolfmark' | Select-Object -ExpandProperty version -First 1
    if (-not $ReleaseDirectory) { $ReleaseDirectory = Join-Path $outRoot "release/$version" }
    $ReleaseDirectory = [IO.Path]::GetFullPath($ReleaseDirectory)
    $setup = Join-Path $ReleaseDirectory 'Wolfmark-Setup-win-x64.exe'
    $portable = Join-Path $ReleaseDirectory 'Wolfmark-portable-win-x64.zip'
    $checksums = Join-Path $ReleaseDirectory 'SHA256SUMS.txt'
    foreach ($path in $setup, $portable, $checksums) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing release artifact: $path" }
    }
    if (Get-ChildItem -LiteralPath $ReleaseDirectory -Filter '*.msi' -File) {
        throw 'The NSIS release directory must not contain an MSI artifact.'
    }
    $checksumText = Get-Content -Raw -LiteralPath $checksums
    foreach ($artifact in $setup, $portable) {
        $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $artifact).Hash.ToLowerInvariant()
        $name = [IO.Path]::GetFileName($artifact)
        if ($checksumText -notmatch [regex]::Escape("$hash *$name")) {
            throw "SHA256SUMS.txt does not match $name."
        }
    }

    $payload = Join-Path $outRoot "package/staging/$version/Wolfmark"
    $manifestPath = Join-Path $payload 'payload-manifest.json'
    $manifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
    if ($manifest.schemaVersion -ne 1 -or $manifest.build.product -ne 'Wolfmark') {
        throw 'The staged canonical payload manifest is invalid.'
    }
    foreach ($file in $manifest.files) {
        $path = Join-Path $payload $file.path
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Manifest file is missing: $($file.path)" }
        if ((Get-Item -LiteralPath $path).Length -ne $file.size) { throw "Manifest size mismatch: $($file.path)" }
        if ((Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLowerInvariant() -ne $file.sha256) {
            throw "Manifest hash mismatch: $($file.path)"
        }
    }

    $portableRoot = Join-Path $outRoot 'tests/nsis-portable-smoke'
    if (Test-Path -LiteralPath $portableRoot) { Remove-Item -Recurse -Force -LiteralPath $portableRoot }
    Expand-Archive -LiteralPath $portable -DestinationPath $portableRoot
    $portableExe = Get-ChildItem -LiteralPath $portableRoot -Filter Wolfmark.exe -Recurse -File | Select-Object -First 1
    if (-not $portableExe) { throw 'Portable archive does not contain Wolfmark.exe.' }
    $savedPath = $env:PATH
    try {
        $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
        $smoke = Start-Process -FilePath $portableExe.FullName -ArgumentList '--smoke-icon' -WorkingDirectory $portableExe.DirectoryName -Wait -PassThru -WindowStyle Hidden
        if ($smoke.ExitCode -ne 0) { throw "Portable smoke failed with exit code $($smoke.ExitCode)." }
    } finally { $env:PATH = $savedPath }

    $runtime = Join-Path $outRoot 'package/nsis/setup-runtime'
    foreach ($required in 'WolfmarkSetup.exe', 'WolfmarkMaintenance.exe', 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'platforms/qwindows.dll') {
        if (-not (Test-Path -LiteralPath (Join-Path $runtime $required) -PathType Leaf)) {
            throw "Setup runtime is missing $required."
        }
    }
    if (Get-ChildItem -LiteralPath $runtime -Recurse -File | Where-Object Name -Match 'Wix|MsiEmbedded|MsiUi') {
        throw 'Obsolete WiX/MSI runtime content remains in the NSIS setup runtime.'
    }
    Write-Host 'NSIS artifact, checksum, payload-manifest, runtime, and portable smoke checks passed.'
    & (Join-Path $PSScriptRoot 'test_setup_startup.ps1')

    if (-not $ExecuteLifecycle) { exit 0 }
    Assert-Administrator
    $existing = Get-ItemProperty -Path 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Wolfmark' -ErrorAction SilentlyContinue
    if ($existing -and -not $AllowExistingWolfmarkReplacement) {
        throw 'Wolfmark is already installed. Re-run only on a clean VM or pass -AllowExistingWolfmarkReplacement deliberately.'
    }
    $lifecycleRoot = Join-Path $outRoot 'tests/nsis-lifecycle'
    $installRoot = Join-Path $lifecycleRoot 'Program Files/Wolfmark'
    $sentinel = Join-Path $lifecycleRoot 'user-document.md'
    New-Item -ItemType Directory -Force $lifecycleRoot | Out-Null
    Set-Content -LiteralPath $sentinel -Value '# User document - installer must preserve this' -Encoding utf8NoBOM

    for ($cycle = 1; $cycle -le 2; $cycle++) {
        Invoke-Setup $setup @('/S', "/D=$installRoot")
        $installedExe = Join-Path $installRoot 'Wolfmark.exe'
        if (-not (Test-Path -LiteralPath $installedExe -PathType Leaf)) { throw "Cycle $cycle did not install Wolfmark.exe." }
        $expected = ($manifest.files | Where-Object path -eq 'Wolfmark.exe').sha256
        Set-Content -LiteralPath $installedExe -Value 'deliberate damage' -Encoding ascii
        Invoke-Setup $setup @('/S', '/REPAIR', "/D=$installRoot")
        if ((Get-FileHash -Algorithm SHA256 -LiteralPath $installedExe).Hash.ToLowerInvariant() -ne $expected) {
            throw "Cycle $cycle repair did not restore the exact cached Wolfmark.exe."
        }
        $uninstaller = Join-Path $env:ProgramData 'Wolfmark/Installer/Wolfmark-Uninstall.exe'
        Invoke-Setup $uninstaller @('/S', '/UNINSTALL')
        if (Test-Path -LiteralPath $installedExe) { throw "Cycle $cycle uninstall left Wolfmark.exe." }
        if (Get-ItemProperty -Path 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Wolfmark' -ErrorAction SilentlyContinue) {
            throw "Cycle $cycle uninstall left Installed Apps registration."
        }
        if (-not (Test-Path -LiteralPath $sentinel -PathType Leaf)) { throw "Cycle $cycle removed a user document." }
    }
    Write-Host 'Two elevated NSIS install/repair/uninstall cycles passed.'
} finally {
    Pop-Location
}
