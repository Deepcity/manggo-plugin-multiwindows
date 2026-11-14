param(
    [Parameter(Mandatory = $true)]
    [string] $QtRoot
)

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$nativeSource = Join-Path $root 'packages/native/src/multiwindows.cpp'
$loaderSource = Join-Path $root 'packages/loader/src/main.cpp'
$outputDirectory = Join-Path $root 'dist/native'
$nativeOutput = Join-Path $outputDirectory 'manggo-multiwindows.dll'
$loaderOutput = Join-Path $outputDirectory 'manggo-multiwindows-loader.exe'
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null

if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    throw 'cl.exe not found. Run this script from an x64 MSVC Developer PowerShell.'
}
foreach ($directory in @('include/QtCore', 'include/QtGui', 'include/QtWidgets')) {
    if (-not (Test-Path -LiteralPath (Join-Path $QtRoot $directory))) {
        throw "Qt include directory is missing: $directory"
    }
}

$includes = @(
    (Join-Path $QtRoot 'include'),
    (Join-Path $QtRoot 'include/QtCore'),
    (Join-Path $QtRoot 'include/QtGui'),
    (Join-Path $QtRoot 'include/QtWidgets')
) | ForEach-Object { "/I$_" }
$common = @(
    '/nologo', '/std:c++17', '/Zc:__cplusplus', '/permissive-', '/utf-8', '/EHsc',
    '/MD', '/O2', '/W4', '/DNDEBUG', '/DUNICODE', '/D_UNICODE', '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX',
    "/Fo:$outputDirectory\\"
)
$nativeArguments = $common + @($nativeSource) + $includes +
    @('/LD', "/Fe:$nativeOutput", '/link', "/LIBPATH:$(Join-Path $QtRoot 'lib')",
      'Qt6Widgets.lib', 'Qt6Gui.lib', 'Qt6Core.lib', 'user32.lib')
& cl.exe @nativeArguments
if ($LASTEXITCODE -ne 0) {
    throw "Native DLL build failed with exit code $LASTEXITCODE."
}

$loaderArguments = $common + @($loaderSource, "/Fe:$loaderOutput", '/link', 'psapi.lib')
& cl.exe @loaderArguments
if ($LASTEXITCODE -ne 0) {
    throw "Loader build failed with exit code $LASTEXITCODE."
}
Write-Output $nativeOutput
Write-Output $loaderOutput
