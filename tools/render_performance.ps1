param([string]$Executable = 'out/cargo/release/moonmark.exe')
$ErrorActionPreference = 'Stop'
foreach ($case in @(
    @('image-geometry', 'fixtures/image-layout-regression.md'),
    @('zoom', 'fixtures/moonmark-visual-test.md'),
    @('zoom', 'fixtures/generated/large-text.md'),
    @('images', 'fixtures/generated/image-stress.md'),
    @('scroll-profile', 'fixtures/generated/large-text.md'),
    @('scroll-profile', 'fixtures/generated/image-stress-250.md'),
    @('scroll-profile', 'fixtures/compatibility/all-features.md')
)) {
    $start = [Diagnostics.ProcessStartInfo]::new((Resolve-Path -LiteralPath $Executable).Path)
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.ArgumentList.Add('--smoke-' + $case[0])
    $start.ArgumentList.Add($case[1])
    $process = [Diagnostics.Process]::Start($start)
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    if (!$process.WaitForExit(60000)) {
        $process.Kill()
        throw "Smoke timed out: $($case -join ' ')"
    }
    Write-Output ($case -join ' ')
    Write-Output $stdout.Result
    Write-Output $stderr.Result
    $code = $process.ExitCode
    Write-Output "EXIT=$code"
    $process.Dispose()
    if ($code -ne 0) { throw "Smoke failed: $($case -join ' ') ($code)" }
}
