$ErrorActionPreference = 'Stop'
if ($env:WOLFMARK_PRIVATE_TEST_DESKTOP -ne '1') {
    throw 'Run this review through scripts/run_background_windows.ps1 -Action gui.'
}
$env:WOLFMARK_DISABLE_UPDATE_CHECKS = '1'
$root = Join-Path $PWD 'out/tests/gui-review'
New-Item -ItemType Directory -Force -Path $root | Out-Null
$env:WOLFMARK_SETTINGS_ROOT = Join-Path $root 'settings'
cargo build --locked --bin wolfmark
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if (!(Test-Path -LiteralPath 'fixtures/generated/image-stress.md')) {
    cargo run --locked --bin generate_stress_fixture
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
$app = (Resolve-Path 'out/cargo/debug/wolfmark.exe').Path
function Invoke-Smoke([string[]]$Arguments) {
    $info = [Diagnostics.ProcessStartInfo]::new($app)
    $info.UseShellExecute = $false
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $info.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::Start($info)
    try {
        $output = $process.StandardOutput.ReadToEndAsync()
        $errors = $process.StandardError.ReadToEndAsync()
        if (!$process.WaitForExit(30000)) {
            $process.Kill($true)
            throw "GUI capture timed out: $Arguments"
        }
        if ($output.Result) { Write-Output $output.Result }
        if ($errors.Result) { Write-Output $errors.Result }
        if ($process.ExitCode -ne 0) { throw "GUI capture failed: $Arguments ($($process.ExitCode))" }
    } finally { $process.Dispose() }
}
foreach ($page in 'updates', 'editor', 'about') {
    $env:WOLFMARK_SNAPSHOT_SETTINGS_PAGE = $page
    Invoke-Smoke @('--smoke-settings-ui')
    Copy-Item -LiteralPath "out/visual/dev7/settings-$page.png" -Destination $root
}
$env:QT_SCALE_FACTOR = '1.5'
$env:WOLFMARK_SNAPSHOT_SETTINGS_PAGE = 'updates'
Invoke-Smoke @('--smoke-settings-ui')
Copy-Item -LiteralPath 'out/visual/dev7/settings-updates.png' -Destination (Join-Path $root 'settings-updates-scale-150.png')
Remove-Item Env:QT_SCALE_FACTOR
foreach ($tab in 'page', 'style', 'details', 'export') {
    $env:WOLFMARK_OUTPUT_TAB = $tab
    $env:WOLFMARK_OUTPUT_SNAPSHOT = Join-Path $root "page-view-$tab.png"
    Invoke-Smoke @('--smoke-output-ui', 'fixtures/wolfmark-visual-test.md')
    if (!(Test-Path -LiteralPath $env:WOLFMARK_OUTPUT_SNAPSHOT)) { throw "Missing $tab capture" }
}
Remove-Item Env:WOLFMARK_OUTPUT_SNAPSHOT
foreach ($mode in 'source', 'live') {
    $env:WOLFMARK_SNAPSHOT_MODE = $mode
    $env:WOLFMARK_SNAPSHOT_NAME = "gui-review-$mode"
    Invoke-Smoke @('--smoke-snapshot', 'fixtures/wolfmark-visual-test.md')
}
Remove-Item Env:WOLFMARK_SNAPSHOT_MODE
& ./tools/visual_snapshots.ps1 -Executable $app
exit 0
