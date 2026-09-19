param([string]$Executable = 'out/cargo/debug/moonmark.exe')
$ErrorActionPreference = 'Stop'
$cases = @(
    @('inline-prose-table', 'fixtures/inline-code-quality.md', 1200, 900, 0),
    @('inline-wrap', 'fixtures/inline-code-quality.md', 720, 1000, 0),
    @('inline-selected', 'fixtures/inline-code-quality.md', 1200, 900, 0, 100, 'SELECTION'),
    @('image-gap-after', 'fixtures/image-layout-regression.md', 1280, 820, 450),
    @('empty', '', 1200, 820, 0),
    @('prose', 'fixtures/moonmark-visual-test.md', 1200, 820, 0),
    @('headings-lists', 'fixtures/moonmark-visual-test.md', 1200, 820, 300),
    @('tables', 'fixtures/document-tables.md', 1200, 960, 0),
    @('code', 'fixtures/code-block-quality.md', 1200, 900, 0),
    @('plain-text', 'fixtures/text/literal.txt', 1200, 900, 0),
    @('narrow', 'fixtures/document-tables.md', 720, 900, 0),
    @('wide', 'fixtures/document-tables.md', 1800, 960, 0),
    @('images', 'fixtures/generated/image-stress.md', 1200, 900, 0),
    @('concept', 'fixtures/concept-presentation.md', 1440, 960, 0),
    @('sidebar-hidden', 'fixtures/concept-presentation.md', 1200, 900, 0, 100, 'NO_SIDEBAR'),
    @('menu', 'fixtures/concept-presentation.md', 1200, 900, 0, 100, 'MENU'),
    @('selection', 'fixtures/concept-presentation.md', 1200, 900, 0, 100, 'SELECTION'),
    @('zoom-80', 'fixtures/concept-presentation.md', 1440, 960, 0, 80),
    @('zoom-100', 'fixtures/concept-presentation.md', 1440, 960, 0, 100),
    @('zoom-125', 'fixtures/concept-presentation.md', 1440, 960, 0, 125),
    @('zoom-150', 'fixtures/concept-presentation.md', 1440, 960, 0, 150),
    @('code-150', 'fixtures/code-block-quality.md', 1200, 900, 0, 150)
)
$names = 'MOONMARK_SNAPSHOT_NAME', 'MOONMARK_SNAPSHOT_WIDTH', 'MOONMARK_SNAPSHOT_HEIGHT', 'MOONMARK_SNAPSHOT_SCROLL',
    'MOONMARK_SNAPSHOT_ZOOM', 'MOONMARK_SNAPSHOT_MENU', 'MOONMARK_SNAPSHOT_SELECTION', 'MOONMARK_SNAPSHOT_NO_SIDEBAR'
$previous = @{}
foreach ($name in $names) { $previous[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
try {
    foreach ($case in $cases) {
        $env:MOONMARK_SNAPSHOT_NAME = $case[0]
        $env:MOONMARK_SNAPSHOT_WIDTH = $case[2]
        $env:MOONMARK_SNAPSHOT_HEIGHT = $case[3]
        $env:MOONMARK_SNAPSHOT_SCROLL = $case[4]
        $env:MOONMARK_SNAPSHOT_ZOOM = if ($case.Count -gt 5) { $case[5] } else { 100 }
        foreach ($mode in 'MENU', 'SELECTION', 'NO_SIDEBAR') {
            $value = if ($case.Count -gt 6 -and $case[6] -eq $mode) { '1' } else { $null }
            [Environment]::SetEnvironmentVariable("MOONMARK_SNAPSHOT_$mode", $value, 'Process')
        }
        $arguments = @('--smoke-snapshot')
        if ($case[1]) {
            if (!(Test-Path -LiteralPath $case[1])) { throw "Missing fixture: $($case[1])" }
            $arguments += $case[1]
        }
        $start = [Diagnostics.ProcessStartInfo]::new((Resolve-Path -LiteralPath $Executable).Path)
        $start.UseShellExecute = $false
        $start.RedirectStandardOutput = $true
        $start.RedirectStandardError = $true
        foreach ($argument in $arguments) { $start.ArgumentList.Add($argument) }
        $process = [Diagnostics.Process]::Start($start)
        $stdout = $process.StandardOutput.ReadToEnd()
        $stderr = $process.StandardError.ReadToEnd()
        $process.WaitForExit()
        Write-Output $stdout
        if ($stderr) { Write-Output $stderr }
        if ($process.ExitCode -ne 0) { throw "Snapshot failed: $($case[0]) ($($process.ExitCode))" }
        $process.Dispose()
    }
} finally {
    foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $previous[$name], 'Process') }
}
