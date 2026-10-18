[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$testRoot = Join-Path $projectRoot 'out/tests/setup-startup'
$runtime = Join-Path $projectRoot 'out/package/nsis/setup-runtime'
$version = ((& cargo metadata --format-version 1 --no-deps) | ConvertFrom-Json).packages |
    Where-Object name -eq 'wolfmark' | Select-Object -ExpandProperty version -First 1
$payload = Join-Path $projectRoot "out/package/staging/$version/Wolfmark"
New-Item -ItemType Directory -Force $testRoot | Out-Null

function Invoke-Checked([string]$Executable, [string[]]$Arguments, [int]$Expected = 0) {
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Executable
    $start.WorkingDirectory = $testRoot # Deliberately not the setup runtime.
    $start.UseShellExecute = $false
    $start.Environment['PATH'] = "$env:SystemRoot\System32;$env:SystemRoot"
    foreach ($argument in $Arguments) { $start.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::Start($start)
    if (-not $process.WaitForExit(45000)) {
        $process.Kill($true)
        throw "Startup contract timed out: $Executable"
    }
    if ($process.ExitCode -ne $Expected) {
        throw "Startup contract expected $Expected, got $($process.ExitCode): $Executable $($Arguments -join ' ')"
    }
}

function Assert-Log([string]$Path, [string[]]$Expected) {
    $text = Get-Content -Raw -LiteralPath $Path
    foreach ($value in $Expected) {
        if (-not $text.Contains($value)) { throw "Missing '$value' in $Path" }
    }
    return $text
}

$savedEnvironment = @{}
foreach ($name in 'WOLFMARK_TEST_CORE_CALLS', 'WOLFMARK_TEST_CORE_STATE', 'WOLFMARK_TEST_CORE_EXIT',
    'WOLFMARK_SETUP_SMOKE_STATE', 'WOLFMARK_SETUP_SCREENSHOT') {
    $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
try {
    Remove-Item Env:WOLFMARK_SETUP_SMOKE_STATE, Env:WOLFMARK_SETUP_SCREENSHOT -ErrorAction SilentlyContinue
    $hostExe = Join-Path $runtime 'WolfmarkSetup.exe'
    $log = Join-Path $testRoot 'direct.log'
    [IO.File]::WriteAllBytes($log, [byte[]]@())
    Invoke-Checked $hostExe @('--silent', "--bootstrap-log=$log") 160
    $null = Assert-Log $log @('stage=startup-failure exit=160', 'requires --maintenance')
    Invoke-Checked $hostExe @('--silent', '--unknown=DO_NOT_LOG_THIS_VALUE', "--bootstrap-log=$log") 160
    if ((Get-Content -Raw $log).Contains('DO_NOT_LOG_THIS_VALUE')) { throw 'Unknown argument was not redacted.' }
    Invoke-Checked $hostExe @('--silent', '--maintenance', "--bootstrap-log=$log") 160
    Invoke-Checked $hostExe @('--silent', '--ui-smoke=typo', "--bootstrap-log=$log") 160

    $common = @("--maintenance=$runtime/WolfmarkMaintenance.exe", "--payload=$payload",
        "--manifest=$payload/payload-manifest.json", "--uninstaller=$hostExe",
        '--install-dir=C:\Program Files\Wolfmark', "--bootstrap-log=$log", '--action=')
    # Actual Rust detection, QApplication/qwindows, real detected state, real window.
    $screenshot = Join-Path $testRoot 'real-core.png'
    if (Test-Path -LiteralPath $screenshot) { Remove-Item -LiteralPath $screenshot } # Prior generated probe only.
    Invoke-Checked $hostExe ($common + "--startup-check=$screenshot")
    $null = Assert-Log $log @('platform=windows', 'stage=window-visible', 'stage=detected status=', 'mode=interactive')
    if (-not (Test-Path $screenshot)) { throw 'The real startup path did not render a window.' }
    Invoke-Checked $hostExe (($common | Where-Object { $_ -notlike '--action=*' }) + '--silent' + '--action=invalid') 160
    Invoke-Checked $hostExe (($common | Where-Object { $_ -notlike '--manifest=*' }) + '--manifest=missing.json' + '--silent') 2

    # Spaces and Unicode are exercised in executable, log, manifest and screenshot paths.
    $probeRuntime = Join-Path $testRoot 'runtime space Ω'
    New-Item -ItemType Directory -Force $probeRuntime | Out-Null
    Copy-Item -Path "$runtime/*" -Destination $probeRuntime -Recurse -Force
    & rustc --edition=2024 -D warnings (Join-Path $projectRoot 'tests/fixtures/setup_core_probe.rs') -o (Join-Path $probeRuntime 'WolfmarkMaintenance.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Maintenance recording double compilation failed.' }
    $env:WOLFMARK_TEST_CORE_CALLS = Join-Path $testRoot 'core-calls.txt'
    $env:WOLFMARK_TEST_CORE_STATE = '{"status":{"kind":"absent"},"state":null}'
    $env:WOLFMARK_TEST_CORE_EXIT = '0'
    Set-Content -LiteralPath $env:WOLFMARK_TEST_CORE_CALLS -Value ''
    $spacePayload = Join-Path $testRoot 'payload space Ω'
    New-Item -ItemType Directory -Force $spacePayload | Out-Null
    Copy-Item -LiteralPath (Join-Path $payload 'payload-manifest.json') -Destination $spacePayload -Force
    $spaceLog = Join-Path $spacePayload 'bootstrap space Ω.log'
    Invoke-Checked (Join-Path $probeRuntime 'WolfmarkSetup.exe') @(
        "--maintenance=$probeRuntime/WolfmarkMaintenance.exe", '--payload', $spacePayload,
        "--manifest=$spacePayload/payload-manifest.json", "--uninstaller=$hostExe",
        '--install-dir=C:\Program Files\Wolfmark Ω', "--bootstrap-log=$spaceLog",
        "--startup-check=$spacePayload/startup space Ω.png"
    )
    $null = Assert-Log $spaceLog @('payload space Ω', 'Wolfmark Ω', 'stage=detected status=absent mode=interactive')

    # Compile the production NSIS source with only test-local substitutions:
    # no UAC for automation; recording core; read-only UI probe; fast compression.
    # No test hooks or runtime preservation switches enter the shipped package.
    $source = Get-Content -Raw (Join-Path $projectRoot 'packaging/windows/nsis/Wolfmark.nsi')
    $source = $source.Replace('RequestExecutionLevel admin', 'RequestExecutionLevel user')
    $source = $source.Replace('SetCompressor /SOLID lzma', 'SetCompress off').Replace('SetCompressorDictSize 64', '')
    $source = $source.Replace('..\..\..\assets\icons\wolfmark.ico', (Join-Path $projectRoot 'assets/icons/wolfmark.ico'))
    $probePng = Join-Path $testRoot 'nsis.png'
    $probeUninstaller = Join-Path $testRoot 'probe-uninstall.exe'
    $source = $source.Replace('  ExecWait $SetupCommand $SetupExitCode', @"
  `${If} `$SetupSilent == ""
    StrCpy `$SetupCommand '`$SetupCommand --startup-check="$probePng"'
  `${EndIf}
  ExecWait `$SetupCommand `$SetupExitCode
"@)
    $source = $source.Replace('  WriteUninstaller "$PLUGINSDIR\Wolfmark-Uninstall.exe"', @"
  WriteUninstaller "`$PLUGINSDIR\Wolfmark-Uninstall.exe"
  CopyFiles /SILENT "`$PLUGINSDIR\Wolfmark-Uninstall.exe" "$probeUninstaller"
"@)
    $probeSource = Join-Path $testRoot 'probe.nsi'
    Set-Content -LiteralPath $probeSource -Value $source -Encoding utf8NoBOM
    $probeExe = Join-Path $testRoot 'probe.exe'
    $nsis = Join-Path $projectRoot 'out/toolchains/nsis/makensis.exe'
    & $nsis /V2 /INPUTCHARSET UTF8 "/DOUTPUT_EXE=$probeExe" "/DSETUP_RUNTIME=$probeRuntime" "/DPAYLOAD_ROOT=$payload" "/DDISPLAY_VERSION=$version" /DWINDOWS_VERSION=0.1.7.0 $probeSource
    if ($LASTEXITCODE -ne 0) { throw 'Startup contract NSIS compilation failed.' }

    if (Test-Path -LiteralPath $probePng) { Remove-Item -LiteralPath $probePng } # Never accept a stale screenshot.
    Invoke-Checked $probeExe @()
    if (-not (Test-Path $probePng)) { throw 'NSIS interactive startup did not render its detected state.' }
    $env:WOLFMARK_TEST_CORE_STATE = '{"status":{"kind":"exact","build":{"version":"0.1.0-dev.7","buildId":"probe"}},"state":{"options":{"installDirectory":"C:/Program Files/Wolfmark","fileAssociations":true,"desktopShortcut":false}}}'
    foreach ($action in '/MAINTENANCE', '/UPDATE', '/REPAIR', '/UNINSTALL') {
        Remove-Item -LiteralPath $probePng
        Invoke-Checked $probeExe @($action)
        if (-not (Test-Path $probePng)) { throw "NSIS $action did not render its detected state." }
    }
    $env:WOLFMARK_TEST_CORE_STATE = '{"status":{"kind":"absent"},"state":null}'
    foreach ($arguments in @(@('/S'), @('/DIAGNOSE'), @('/REPAIR', '/S'), @('/UNINSTALL', '/S'))) {
        Invoke-Checked $probeExe $arguments
    }
    Invoke-Checked $probeUninstaller @('/S', '/UNINSTALL')
    $calls = Get-Content -Raw $env:WOLFMARK_TEST_CORE_CALLS
    foreach ($operation in 'install', 'repair', 'uninstall') {
        if (-not $calls.Contains('["' + $operation + '"')) { throw "NSIS did not route $operation to the core." }
    }
    if ($calls.Contains('--cancel-file')) { throw 'Silent operations passed a cancellation file.' }
    $env:WOLFMARK_TEST_CORE_EXIT = '42'
    Invoke-Checked $probeExe @('/DIAGNOSE') 42
    $bootstrap = Get-ChildItem $env:TEMP -Filter 'Wolfmark-Setup-bootstrap-*.log' |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    $null = Assert-Log $bootstrap.FullName @('stage=bootstrap', 'stage=extracted', 'stage=launch cwd=',
        'stage=host-entry', 'child-exit=42', 'stage=child-exit component=WolfmarkSetup exit=42')

    $failureSource = $source.Replace('StrCpy $SetupCommand ''"$PLUGINSDIR\setup\WolfmarkSetup.exe"',
        'StrCpy $SetupCommand ''"$PLUGINSDIR\setup\MissingSetup.exe"')
    $failureNsi = Join-Path $testRoot 'failure.nsi'
    $failureExe = Join-Path $testRoot 'failure.exe'
    Set-Content -LiteralPath $failureNsi -Value $failureSource -Encoding utf8NoBOM
    & $nsis /V2 /INPUTCHARSET UTF8 "/DOUTPUT_EXE=$failureExe" "/DSETUP_RUNTIME=$probeRuntime" "/DPAYLOAD_ROOT=$payload" "/DDISPLAY_VERSION=$version" /DWINDOWS_VERSION=0.1.7.0 $failureNsi
    if ($LASTEXITCODE -ne 0) { throw 'Failure-path NSIS compilation failed.' }
    Invoke-Checked $failureExe @('/S') 1603
    $bootstrap = Get-ChildItem $env:TEMP -Filter 'Wolfmark-Setup-bootstrap-*.log' |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    $null = Assert-Log $bootstrap.FullName @('stage=failure component=Wolfmark Setup UI (process creation failed) exit=1603')
    Write-Host 'Setup startup contract passed: real Qt/core detection, NSIS extraction/arguments, modes, Unicode/spaces, cancellation arguments, and exit propagation.'
} finally {
    foreach ($name in $savedEnvironment.Keys) { [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process') }
}
