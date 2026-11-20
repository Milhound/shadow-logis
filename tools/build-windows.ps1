# Run from an x64 Visual Studio Developer PowerShell; no system changes here.
[CmdletBinding()]
param(
    [string]$QtPrefix = $env:QT_ROOT_DIR,
    [ValidateRange(1, 1024)][int]$Jobs = 4
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Invoke-Checked {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}

if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'Run this script on Windows in an x64 Visual Studio Developer PowerShell.'
}
if ($env:VSCMD_ARG_TGT_ARCH -notin @('x64', 'amd64')) {
    throw 'Select an x64 Visual Studio 2022 developer environment before building.'
}
foreach ($Tool in @('cl.exe', 'cmake.exe', 'ctest.exe', 'ninja.exe', 'python.exe', 'pdftotext.exe', 'pdftoppm.exe')) {
    if (-not (Get-Command $Tool -ErrorAction SilentlyContinue)) { throw "Missing $Tool; see README.md Windows prerequisites." }
}
Invoke-Checked 'python.exe' @('-c', 'import pefile')
if (-not $QtPrefix) { $QtPrefix = $env:CMAKE_PREFIX_PATH }
if (-not $QtPrefix -or -not (Test-Path -LiteralPath (Join-Path $QtPrefix 'lib/cmake/Qt6Pdf/Qt6PdfConfig.cmake'))) {
    throw 'Pass -QtPrefix with one matching MSVC x64 Qt installation including Qt PDF/PDF Widgets.'
}
$QtPrefix = (Resolve-Path -LiteralPath $QtPrefix).Path
if (-not (Test-Path -LiteralPath (Join-Path $QtPrefix 'bin/windeployqt.exe'))) {
    throw 'The selected Qt kit has no windeployqt.exe deployment tool.'
}
$ProjectDir = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $ProjectDir 'build-windows'
$PackageRoot = Join-Path $ProjectDir 'package'
$OriginalPath = $env:PATH
try {
    # Uninstalled test executables find the selected Qt kit; deployment validation
    # later runs with only Windows system directories in PATH.
    $env:PATH = (Join-Path $QtPrefix 'bin') + ';' + $OriginalPath
    Invoke-Checked 'cmake.exe' @('-S', $ProjectDir, '-B', $BuildDir, '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_CXX_COMPILER=cl',
        "-DCMAKE_PREFIX_PATH=$QtPrefix", '-DCMAKE_INSTALL_BINDIR=bin', '-DBUILD_TESTING=ON')
    Invoke-Checked 'cmake.exe' @('--build', $BuildDir, '--parallel', "$Jobs")
    Invoke-Checked 'ctest.exe' @('--test-dir', $BuildDir, '--output-on-failure')
    New-Item -ItemType Directory -Force -Path $PackageRoot | Out-Null
    $Stage = Join-Path $PackageRoot ('.windows-stage-' + [guid]::NewGuid().ToString('N'))
    Invoke-Checked 'cmake.exe' @('--install', $BuildDir, '--prefix', $Stage)
    # Use FreeType for embedded variable fonts with the native Windows platform.
    Add-Content -LiteralPath (Join-Path $Stage 'bin/qt.conf') -Encoding ascii -Value @(
        '', '[Platforms]', 'WindowsArguments = fontengine=freetype')
    # Qt may deploy drivers for database servers that the application never uses.
    $SqlDrivers = Join-Path $Stage 'plugins/sqldrivers'
    if (Test-Path -LiteralPath $SqlDrivers) {
        Get-ChildItem -LiteralPath $SqlDrivers -File -Filter '*.dll' |
            Where-Object Name -NE 'qsqlite.dll' |
            ForEach-Object { Remove-Item -LiteralPath $_.FullName }
    }
    Invoke-Checked 'python.exe' @((Join-Path $PSScriptRoot 'verify_windows_bundle.py'), $Stage)
    $Destination = Join-Path $PackageRoot 'windows'
    if (Test-Path -LiteralPath $Destination) {
        $Previous = Join-Path $PackageRoot ('windows-previous-' + [guid]::NewGuid().ToString('N'))
        Move-Item -LiteralPath $Destination -Destination $Previous
        Write-Host "Previous package preserved in $Previous"
    }
    Move-Item -LiteralPath $Stage -Destination $Destination
    Write-Host "Built $(Join-Path $Destination 'bin/shadow-logis.exe')"
    Write-Host 'Copy the whole package/windows folder, then create/open a workspace or import a legacy TPS installation.'
    Write-Host 'Requires the official Microsoft Visual C++ x64 Redistributable. This development package is unsigned.'
} finally {
    $env:PATH = $OriginalPath
}
