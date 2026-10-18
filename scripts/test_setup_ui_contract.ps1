[CmdletBinding()]
param([string]$QtRoot)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$runtime = Join-Path $projectRoot 'out/package/nsis/setup-runtime'
$testRoot = Join-Path $projectRoot 'out/tests/setup-ui-contract'
if (-not $QtRoot) { $QtRoot = if ($env:WOLFMARK_QT_DIR) { $env:WOLFMARK_QT_DIR } else { Join-Path $projectRoot 'out/toolchains/qt' } }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$visualStudio = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
if ($LASTEXITCODE -ne 0 -or -not $visualStudio) { throw 'The setup action tests require the existing MSVC toolchain.' }
New-Item -ItemType Directory -Force "$testRoot/bin", "$testRoot/obj" | Out-Null
$start = [Diagnostics.ProcessStartInfo]::new()
$start.FileName = Join-Path $visualStudio 'MSBuild/Current/Bin/MSBuild.exe'
$start.WorkingDirectory = $projectRoot
$start.UseShellExecute = $false
$start.Environment['PATH'] = $env:PATH
foreach ($argument in @(
    'packaging/windows/setup-ui/WolfmarkSetup.vcxproj', '/t:Build', '/p:Configuration=Release', '/p:Platform=x64',
    '/p:WolfmarkSetupContractTests=true', "/p:QtRoot=$QtRoot", "/p:ProjectRoot=$projectRoot",
    "/p:WolfmarkOutputDir=$testRoot/bin", "/p:WolfmarkIntermediateDir=$testRoot/obj",
    "/p:WolfmarkGeneratedDir=$projectRoot/out/package/nsis/setup-build/generated", '/m:1', '/nr:false', '/v:minimal'
)) { $start.ArgumentList.Add($argument) }
$build = [Diagnostics.Process]::Start($start)
$build.WaitForExit()
if ($build.ExitCode -ne 0) { throw 'Native setup action tests did not compile.' }

# Keep test executables out of the packaged runtime. Reuse its app-local Qt files.
foreach ($name in 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'qt.conf', 'platforms', 'assets',
    'msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll', 'vcruntime140.dll', 'vcruntime140_1.dll') {
    Copy-Item -LiteralPath (Join-Path $runtime $name) -Destination "$testRoot/bin" -Recurse -Force
}
$core = Join-Path $testRoot 'bin/SetupCoreProbe.exe'
& rustc --edition=2024 -D warnings (Join-Path $projectRoot 'tests/fixtures/setup_core_probe.rs') -o $core
if ($LASTEXITCODE -ne 0) { throw 'Recording maintenance core compilation failed.' }
$manifest = Join-Path $testRoot 'payload-manifest.json'
Set-Content -LiteralPath $manifest -Value '{}'
$calls = Join-Path $testRoot 'core-calls.txt'
Set-Content -LiteralPath $calls -Value ''
$run = [Diagnostics.ProcessStartInfo]::new()
$run.FileName = Join-Path $testRoot 'bin/WolfmarkSetupContractTests.exe'
$run.WorkingDirectory = $testRoot
$run.UseShellExecute = $false
$run.Environment['PATH'] = "$env:SystemRoot\System32;$env:SystemRoot"
$run.Environment['WOLFMARK_TEST_UI_CORE'] = $core
$run.Environment['WOLFMARK_TEST_UI_MANIFEST'] = $manifest
$run.Environment['WOLFMARK_TEST_CORE_CALLS'] = $calls
$run.Environment['WOLFMARK_TEST_CORE_STATE'] = '{"status":{"kind":"same-version-installed-ancestor","installed":{"version":"0.1.0-dev.7","buildId":"installed"}},"state":{"options":{"installDirectory":"D:/Installed apps/Wolfmark Ω","fileAssociations":false,"desktopShortcut":true}}}'
$run.Environment['WOLFMARK_TEST_EXPECT_INSTALL_DIR'] = 'D:/Installed apps/Wolfmark Ω'
$run.Environment['WOLFMARK_TEST_CORE_EXIT'] = '0'
$run.Environment['WOLFMARK_TEST_ALLOW_CANCEL'] = '1'
$process = [Diagnostics.Process]::Start($run)
if (-not $process.WaitForExit(30000)) {
    $process.Kill($true)
    throw 'Native setup action tests timed out.'
}
if ($process.ExitCode -ne 0) { throw "Native setup action contracts failed with exit $($process.ExitCode)." }
if (-not (Get-Content -Raw -LiteralPath $calls).Contains('["install"')) { throw 'The native update did not reach the recording core.' }
