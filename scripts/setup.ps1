[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$qtRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'out/toolchains/qt'))
$wix = [IO.Path]::GetFullPath((Join-Path $projectRoot 'out/toolchains/wix/wix.exe'))
$pwsh = (Get-Command pwsh -ErrorAction Stop).Source

function Assert-MsvcToolchain {
    $compiler = Get-Command cl.exe -ErrorAction SilentlyContinue
    if ($compiler) {
        Write-Host "MSVC C++ compiler is available at $($compiler.Source)"
    } else {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
            throw 'Visual Studio Build Tools were not found. Install the MSVC C++ x64 build tools and Windows SDK, then run cargo setup again.'
        }

        $installation = (& $vswhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath).Trim()
        if ($LASTEXITCODE -ne 0 -or -not $installation) {
            throw 'Visual Studio is installed, but the MSVC C++ x64 build tools are missing. Add the Desktop development with C++ workload and a Windows SDK.'
        }

        $compiler = Get-ChildItem -LiteralPath (Join-Path $installation 'VC/Tools/MSVC') `
            -Filter cl.exe -File -Recurse -ErrorAction SilentlyContinue |
            Where-Object FullName -Like '*\bin\Hostx64\x64\cl.exe' |
            Sort-Object FullName -Descending | Select-Object -First 1
        if (-not $compiler) {
            throw "The Visual Studio installation at $installation does not contain the x64 MSVC compiler."
        }
        Write-Host "MSVC C++ compiler is available at $($compiler.FullName)"
    }

    $resourceCompiler = Get-Command rc.exe -ErrorAction SilentlyContinue
    if (-not $resourceCompiler) {
        $sdkBin = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10/bin'
        if (Test-Path -LiteralPath $sdkBin -PathType Container) {
            $resourceCompiler = Get-ChildItem -LiteralPath $sdkBin -Filter rc.exe -File -Recurse `
                -ErrorAction SilentlyContinue |
                Where-Object FullName -Like '*\x64\rc.exe' |
                Sort-Object FullName -Descending | Select-Object -First 1
        }
    }
    if (-not $resourceCompiler) {
        throw 'The Windows SDK resource compiler (rc.exe) was not found. Install a Windows SDK through Visual Studio Build Tools.'
    }
    $resourceCompilerPath = if ($resourceCompiler.Source) {
        $resourceCompiler.Source
    } else {
        $resourceCompiler.FullName
    }
    Write-Host "Windows SDK resource compiler is available at $resourceCompilerPath"
}

function Test-QtSdk {
    foreach ($library in 'Qt6Core.lib', 'Qt6Gui.lib', 'Qt6Widgets.lib', 'Qt6Network.lib') {
        if (-not (Test-Path -LiteralPath (Join-Path $qtRoot "lib/$library") -PathType Leaf)) {
            return $false
        }
    }
    return $true
}

function Invoke-Bootstrap([string]$ScriptName) {
    & $pwsh -NoProfile -File (Join-Path $PSScriptRoot $ScriptName)
    if ($LASTEXITCODE -ne 0) {
        throw "$ScriptName failed with exit code $LASTEXITCODE."
    }
}

Push-Location $projectRoot
try {
    Assert-MsvcToolchain

    if (Test-QtSdk) {
        Write-Host "Project-local Qt is already available at $qtRoot"
    } else {
        Write-Host 'Project-local Qt is missing; running the existing verified Qt bootstrap.'
        Invoke-Bootstrap 'bootstrap_qt.ps1'
        if (-not (Test-QtSdk)) {
            throw "Qt bootstrap completed, but the required Core, Gui, Widgets, and Network libraries are not all present at $qtRoot."
        }
    }

    Invoke-Bootstrap 'bootstrap_wix.ps1'
    if (-not (Test-Path -LiteralPath $wix -PathType Leaf)) {
        throw "WiX bootstrap completed, but wix.exe is missing at $wix."
    }

    Write-Host 'Moonmark development tooling is ready.'
} catch {
    Write-Error "Moonmark setup failed: $($_.Exception.Message)"
    exit 1
} finally {
    Pop-Location
}
