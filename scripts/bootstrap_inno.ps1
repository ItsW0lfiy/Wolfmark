[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$version = '7.1.0'
$expectedHash = '0362A383ED217D4C4239B5933866DD96D3EB2102737DA92F80F6057A4B40DF2F'
$url = 'https://github.com/jrsoftware/issrc/releases/download/is-7_1_0/innosetup-7.1.0-x64.exe'
$projectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$toolsRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'out/toolchains/inno'))
$downloadRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'out/cache/inno'))
$installer = Join-Path $downloadRoot "innosetup-$version-x64.exe"
$toolRoot = Join-Path $toolsRoot $version
$compiler = Join-Path $toolRoot 'ISCC.exe'

if (Test-Path -LiteralPath $compiler -PathType Leaf) {
    $reportedVersion = (& $compiler --version).Trim()
    if ($LASTEXITCODE -eq 0 -and $reportedVersion -eq $version) {
        Write-Host "Inno Setup $version is already available at $compiler"
        exit 0
    }
    throw "Unexpected Inno Setup compiler at $compiler (reported '$reportedVersion')."
}

New-Item -ItemType Directory -Force -Path $downloadRoot | Out-Null
if (-not (Test-Path -LiteralPath $installer -PathType Leaf) -or
    (Get-FileHash -Algorithm SHA256 -LiteralPath $installer).Hash -ne $expectedHash) {
    Invoke-WebRequest -Uri $url -OutFile $installer
}

$actualHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $installer).Hash
if ($actualHash -ne $expectedHash) {
    throw "Inno Setup download hash mismatch. Expected $expectedHash, got $actualHash."
}
$signature = Get-AuthenticodeSignature -LiteralPath $installer
if ($signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid -or
    $signature.SignerCertificate.Subject -notlike 'CN=Pyrsys B.V.*') {
    throw "Inno Setup Authenticode verification failed: $($signature.Status) $($signature.StatusMessage)"
}

$arguments = @(
    '/PORTABLE=1',
    '/VERYSILENT',
    '/CURRENTUSER',
    "/DIR=$toolRoot",
    '/SUPPRESSMSGBOXES',
    '/NORESTART',
    '/NOICONS'
)
$process = Start-Process -FilePath $installer -ArgumentList $arguments -Wait -PassThru -WindowStyle Hidden
if ($process.ExitCode -ne 0) {
    throw "Project-local Inno Setup extraction failed with exit code $($process.ExitCode)."
}
if (-not (Test-Path -LiteralPath $compiler -PathType Leaf)) {
    throw "Inno Setup compiler was not created at $compiler."
}
$reportedVersion = (& $compiler --version).Trim()
if ($LASTEXITCODE -ne 0 -or $reportedVersion -ne $version) {
    throw "Expected Inno Setup $version, got '$reportedVersion'."
}
Write-Host "Inno Setup $version is ready at $compiler"
