param([string]$Executable = 'out/cargo/release/wolfmark.exe')
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$fixtureRoot = Join-Path $projectRoot 'out/tests/editor-performance'
[IO.Directory]::CreateDirectory($fixtureRoot) | Out-Null
$source = [Text.StringBuilder]::new()
for ($index = 1; $index -le 10000; $index++) {
    if ($index % 100 -eq 1) { [void]$source.Append("# Chapter $index`n`n") }
    [void]$source.Append("Paragraph $index **bold** and _emphasis_ with literal source and Unicode 月🐺. ")
    [void]$source.Append(('Preserve whitespace, links and source while typing. ' * 5))
    [void]$source.Append("`n`n")
}
$large = Join-Path $fixtureRoot 'multi-megabyte.md'
[IO.File]::WriteAllText($large, $source.ToString(), [Text.UTF8Encoding]::new($false))
$source = $null
$executablePath = (Resolve-Path -LiteralPath $Executable).Path
foreach ($fixture in @(
    (Join-Path $projectRoot 'fixtures/generated/large-text.md'),
    (Join-Path $projectRoot 'fixtures/generated/large-literal.txt'),
    $large
)) {
    if (!(Test-Path -LiteralPath $fixture -PathType Leaf)) { throw "Missing fixture: $fixture. Run the existing fixture generators first." }
    $start = [Diagnostics.ProcessStartInfo]::new($executablePath)
    $start.UseShellExecute = $false
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.WorkingDirectory = $projectRoot
    $start.ArgumentList.Add('--smoke-editor-performance')
    $start.ArgumentList.Add($fixture)
    $elapsed = [Diagnostics.Stopwatch]::StartNew()
    $process = [Diagnostics.Process]::Start($start)
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    $workingPeak = 0L
    $privatePeak = 0L
    try {
        while (!$process.WaitForExit(100)) {
            if ($elapsed.Elapsed.TotalMinutes -gt 2) {
                $process.Kill()
                throw "Editor probe did not complete: $fixture"
            }
            try {
                $process.Refresh()
                $workingPeak = [Math]::Max($workingPeak, $process.WorkingSet64)
                $privatePeak = [Math]::Max($privatePeak, $process.PrivateMemorySize64)
            } catch [InvalidOperationException] {
                if (!$process.HasExited) { throw }
            }
        }
        $process.WaitForExit()
        Write-Output "EDITOR_FIXTURE path=$fixture bytes=$((Get-Item -LiteralPath $fixture).Length) sampled_working_peak_mib=$([Math]::Round($workingPeak / 1MB, 1)) sampled_private_peak_mib=$([Math]::Round($privatePeak / 1MB, 1)) elapsed_ms=$($elapsed.ElapsedMilliseconds)"
        Write-Output $stdout.Result
        if ($stderr.Result) { Write-Output $stderr.Result }
        if ($process.ExitCode -ne 0) { throw "Editor probe failed ($($process.ExitCode)): $fixture" }
    } finally {
        $process.Dispose()
    }
}
