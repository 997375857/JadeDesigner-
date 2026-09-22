param([Parameter(Mandatory=$true)][string]$Library)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vsRoot) { throw 'Visual C++ tools not found' }
$vcvars = Join-Path $vsRoot 'VC\Auxiliary\Build\vcvars32.bat'
$out = Join-Path $repo 'bin\tests'
$exe = Join-Path $out 'PluginLifecycleTests.exe'
$compile = 'call "{0}" >nul && cl /nologo /std:c++20 /EHsc /W4 /utf-8 /D_CRT_SECURE_NO_WARNINGS /I"{1}\elib" "{1}\tests\PluginLifecycleTests.cpp" "{1}\src\PluginEntry.cpp" "{1}\elib\fnshare.cpp" /Fe:"{2}" /link user32.lib' -f $vcvars,$repo,$exe
Push-Location $out
try {
    & cmd.exe /d /s /c $compile
    if ($LASTEXITCODE) { throw 'Plugin lifecycle test build failed' }
    & $exe $Library
    if ($LASTEXITCODE) { throw "Plugin lifecycle tests failed: $LASTEXITCODE" }
} finally { Pop-Location }
