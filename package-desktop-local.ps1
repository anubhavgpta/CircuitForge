param(
    [string]$QtBin = 'C:\Qt\6.12.0\mingw_64\bin',
    [string]$BuildDirectory = 'build-desktop',
    [string]$Engine = 'build-gpu\CircuitForge.exe'
)
$ErrorActionPreference = 'Stop'
$ProjectRoot = $PSScriptRoot
$BuildRoot = [IO.Path]::GetFullPath((Join-Path $ProjectRoot $BuildDirectory))
$PackageRoot = Join-Path $BuildRoot 'package'
$DesktopExecutable = Join-Path $BuildRoot 'CircuitForgeDesktop.exe'
if (-not (Test-Path -LiteralPath $DesktopExecutable)) { throw 'Build the desktop target first.' }
New-Item -ItemType Directory -Force -Path $PackageRoot | Out-Null
Copy-Item -LiteralPath $DesktopExecutable -Destination (Join-Path $PackageRoot 'CircuitForgeDesktop.exe') -Force
$env:PATH = "$QtBin;C:\Qt\Tools\mingw1310_64\bin;$env:PATH"
& (Join-Path $QtBin 'windeployqt.exe') --release --no-translations --no-opengl-sw --dir $PackageRoot (Join-Path $PackageRoot 'CircuitForgeDesktop.exe')
if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed.' }
$EnginePath = [IO.Path]::GetFullPath((Join-Path $ProjectRoot $Engine))
if (Test-Path -LiteralPath $EnginePath) {
    Copy-Item -LiteralPath $EnginePath -Destination (Join-Path $PackageRoot 'CircuitForge.exe') -Force
} else {
    Write-Warning 'Engine not found; choose the CLI in Engine Settings after launch.'
}
New-Item -ItemType Directory -Force -Path (Join-Path $PackageRoot 'circuits') | Out-Null
foreach ($Name in 'Example.txt','HalfAdder.txt','FullAdder.txt') {
    Copy-Item -LiteralPath (Join-Path $ProjectRoot "circuits\$Name") -Destination (Join-Path $PackageRoot 'circuits') -Force
}
Write-Host "Ready: $PackageRoot\CircuitForgeDesktop.exe"
Write-Host 'This is a local development package. Review Qt and toolchain redistribution licenses before distributing it.'
