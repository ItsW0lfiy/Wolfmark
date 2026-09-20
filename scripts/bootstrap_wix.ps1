[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$version = '7.0.0'
$projectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$outRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'out'))
$toolRoot = Join-Path $outRoot 'toolchains/wix'
$wix = Join-Path $toolRoot 'wix.exe'
$cacheRoot = Join-Path $outRoot 'cache/wix'
$nugetPackages = Join-Path $outRoot 'cache/nuget'
$dotnetHome = Join-Path $outRoot 'cache/dotnet-home'
$appData = Join-Path $dotnetHome 'AppData/Roaming'
$localAppData = Join-Path $dotnetHome 'AppData/Local'
$nugetConfig = Join-Path $cacheRoot 'NuGet.Config'
$dependencyProject = Join-Path $projectRoot 'packaging/windows/bootstrapper/WolfmarkBootstrapperDependencies.csproj'

function Assert-WixVersion {
    if (-not (Test-Path -LiteralPath $wix -PathType Leaf)) { return $false }
    $reported = (& $wix --version).Trim()
    if ($LASTEXITCODE -ne 0 -or -not $reported.StartsWith($version, [StringComparison]::Ordinal)) {
        throw "Unexpected WiX executable at $wix (reported '$reported')."
    }
    return $true
}

Push-Location $projectRoot
try {
    if (-not (Get-Command dotnet -ErrorAction SilentlyContinue)) {
        throw 'The .NET SDK is required at build time to restore the WiX tool and native bootstrapper API package. Install .NET SDK 8 or newer, then run cargo setup again.'
    }
    New-Item -ItemType Directory -Force $toolRoot, $cacheRoot, $nugetPackages, $dotnetHome, $appData, $localAppData | Out-Null
    Set-Content -LiteralPath $nugetConfig -Encoding utf8NoBOM -Value @'
<?xml version="1.0" encoding="utf-8"?>
<configuration>
  <packageSources>
    <clear />
    <add key="nuget.org" value="https://api.nuget.org/v3/index.json" protocolVersion="3" />
  </packageSources>
</configuration>
'@
    $env:DOTNET_CLI_HOME = $dotnetHome
    $env:APPDATA = $appData
    $env:LOCALAPPDATA = $localAppData
    $env:NUGET_PACKAGES = $nugetPackages
    $env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'

    if (-not (Assert-WixVersion)) {
        & dotnet tool install wix --version $version --tool-path $toolRoot --configfile $nugetConfig
        if ($LASTEXITCODE -ne 0) { throw 'WiX tool restore failed.' }
    }
    if (-not (Assert-WixVersion)) { throw "WiX $version was not created at $wix." }

    $restoreRoot = Join-Path $cacheRoot 'bootstrapper-dependencies'
    & dotnet restore $dependencyProject --packages $nugetPackages --configfile $nugetConfig --locked-mode `
        --property:BaseIntermediateOutputPath="$restoreRoot/obj/" `
        --property:MSBuildProjectExtensionsPath="$restoreRoot/obj/" `
        --property:NuGetAudit=false
    if ($LASTEXITCODE -ne 0) { throw 'WiX native bootstrapper API restore failed.' }

    # This explicit switch records acceptance for every WiX 7 invocation without
    # modifying a user-profile or machine-wide EULA marker.
    & $wix --acceptEula wix7 --version | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'WiX 7 EULA acceptance verification failed.' }
    Write-Host "WiX Toolset $version is ready at $wix"
} catch {
    Write-Error "WiX bootstrap failed: $($_.Exception.Message)"
    exit 1
} finally {
    Pop-Location
}
