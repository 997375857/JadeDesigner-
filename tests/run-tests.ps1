$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vsRoot) { throw 'Visual C++ tools not found' }
$vcvars = Join-Path $vsRoot 'VC\Auxiliary\Build\vcvars32.bat'
$out = Join-Path $repo 'bin\tests'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$testSource = Join-Path $PSScriptRoot 'ProjectAssemblyTests.cpp'
$source = Join-Path $repo 'src\ProjectAssembly.cpp'
$exe = Join-Path $out 'ProjectAssemblyTests.exe'
$compile = 'call "{0}" >nul && cl /nologo /std:c++20 /EHsc /W4 /WX /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX "{1}" "{2}" /Fe:"{3}" /link user32.lib comctl32.lib' -f $vcvars,$testSource,$source,$exe
Push-Location $out
try {
    & cmd.exe /d /s /c $compile
    if ($LASTEXITCODE -ne 0) { throw 'Test build failed' }
    & $exe
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
    $hook = Join-Path $repo '..\jadehook\bin\Release\jadehook.dll'
    $contractSource = Join-Path $PSScriptRoot 'MemoryBridgeContractTests.cpp'
    $contractExe = Join-Path $out 'MemoryBridgeContractTests.exe'
    $contractCompile = 'call "{0}" >nul && cl /nologo /std:c++20 /EHsc /W4 /WX /utf-8 "{1}" /Fe:"{2}"' -f $vcvars,$contractSource,$contractExe
    & cmd.exe /d /s /c $contractCompile
    if ($LASTEXITCODE -ne 0) { throw 'Bridge contract build failed' }
    & $contractExe $hook
    if ($LASTEXITCODE -ne 0) { throw 'Bridge contract tests failed' }
} finally { Pop-Location }
