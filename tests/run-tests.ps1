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
    $toolsSource = Join-Path $PSScriptRoot 'DesignerToolsTests.cpp'
    $toolsExe = Join-Path $out 'DesignerToolsTests.exe'
    $toolsCompile = 'call "{0}" >nul && cl /nologo /std:c++20 /EHsc /W4 /WX /utf-8 /DNOMINMAX "{1}" /Fe:"{2}" /link Shlwapi.lib' -f $vcvars,$toolsSource,$toolsExe
    & cmd.exe /d /s /c $toolsCompile
    if ($LASTEXITCODE -ne 0) { throw 'Designer template test build failed' }
    & $toolsExe
    if ($LASTEXITCODE -ne 0) { throw 'Designer template tests failed' }
    $designSource = Join-Path $PSScriptRoot 'DesignWorkspaceStoreTests.cpp'
    $designExe = Join-Path $out 'DesignWorkspaceStoreTests.exe'
    $designCompile = 'call "{0}" >nul && cl /nologo /std:c++20 /EHsc /W4 /WX /utf-8 "{1}" /Fe:"{2}" /link Shlwapi.lib' -f $vcvars,$designSource,$designExe
    & cmd.exe /d /s /c $designCompile
    if ($LASTEXITCODE -ne 0) { throw 'Design store test build failed' }
    & $designExe
    if ($LASTEXITCODE -ne 0) { throw 'Design store tests failed' }
    & cmd.exe /d /s /c $compile
    if ($LASTEXITCODE -ne 0) { throw 'Test build failed' }
    & $exe
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
    $inspectionSource = Join-Path $PSScriptRoot 'DesignerInspectionTests.cpp'
    $inspectionExe = Join-Path $out 'DesignerInspectionTests.exe'
    $inspectionCompile = 'call "{0}" >nul && cl /nologo /std:c++20 /EHsc /W4 /WX /utf-8 "{1}" /Fe:"{2}"' -f $vcvars,$inspectionSource,$inspectionExe
    & cmd.exe /d /s /c $inspectionCompile
    if ($LASTEXITCODE -ne 0) { throw 'Designer inspection test build failed' }
    & $inspectionExe
    if ($LASTEXITCODE -ne 0) { throw 'Designer inspection tests failed' }
    $webDetectionSource = Join-Path $PSScriptRoot 'ProjectWebDetectionTests.cpp'
    $webDetectionExe = Join-Path $out 'ProjectWebDetectionTests.exe'
    $webDetectionCompile = 'call "{0}" >nul && cl /nologo /std:c++20 /EHsc /W4 /WX /utf-8 "{1}" /Fe:"{2}"' -f $vcvars,$webDetectionSource,$webDetectionExe
    & cmd.exe /d /s /c $webDetectionCompile
    if ($LASTEXITCODE -ne 0) { throw 'Project web detection test build failed' }
    & $webDetectionExe
    if ($LASTEXITCODE -ne 0) { throw 'Project web detection tests failed' }
    $listSource = Join-Path $PSScriptRoot 'ListEventBindingTests.cpp'
    $listExe = Join-Path $out 'ListEventBindingTests.exe'
    $listCompile = 'call "{0}" >nul && cl /nologo /std:c++20 /EHsc /W4 /WX /utf-8 "{1}" /Fe:"{2}"' -f $vcvars,$listSource,$listExe
    & cmd.exe /d /s /c $listCompile
    if ($LASTEXITCODE -ne 0) { throw 'List event test build failed' }
    & $listExe
    if ($LASTEXITCODE -ne 0) { throw 'List event tests failed' }
    $dockSource = Join-Path $PSScriptRoot 'NativeVisualDockTests.cpp'
    $dockExe = Join-Path $out 'NativeVisualDockTests.exe'
    $dockCompile = 'call "{0}" >nul && cl /nologo /std:c++20 /EHsc /W4 /WX /utf-8 "{1}" /Fe:"{2}" /link user32.lib comctl32.lib gdi32.lib' -f $vcvars,$dockSource,$dockExe
    & cmd.exe /d /s /c $dockCompile
    if ($LASTEXITCODE -ne 0) { throw 'Native property test build failed' }
    & $dockExe
    if ($LASTEXITCODE -ne 0) { throw 'Native property tests failed' }
    $diagnosticSource = Join-Path $PSScriptRoot 'NativeDiagnosticsDockTests.cpp'
    $diagnosticExe = Join-Path $out 'NativeDiagnosticsDockTests.exe'
    $diagnosticCompile = 'call "{0}" >nul && cl /nologo /std:c++20 /EHsc /W4 /WX /utf-8 "{1}" /Fe:"{2}" /link user32.lib comctl32.lib gdi32.lib' -f $vcvars,$diagnosticSource,$diagnosticExe
    & cmd.exe /d /s /c $diagnosticCompile
    if ($LASTEXITCODE -ne 0) { throw 'Native diagnostics test build failed' }
    & $diagnosticExe
    if ($LASTEXITCODE -ne 0) { throw 'Native diagnostics tests failed' }
    & (Join-Path $PSScriptRoot 'PreviewHostSafetyTests.ps1') -Source (Join-Path $repo 'src\WebPreview.cpp')
    if ($LASTEXITCODE -ne 0) { throw 'Preview host safety tests failed' }
    $hook = Join-Path $repo 'jadehook\bin\Release\jadehook.dll'
    $contractSource = Join-Path $PSScriptRoot 'MemoryBridgeContractTests.cpp'
    $contractExe = Join-Path $out 'MemoryBridgeContractTests.exe'
    $contractCompile = 'call "{0}" >nul && cl /nologo /std:c++20 /EHsc /W4 /WX /utf-8 "{1}" /Fe:"{2}"' -f $vcvars,$contractSource,$contractExe
    & cmd.exe /d /s /c $contractCompile
    if ($LASTEXITCODE -ne 0) { throw 'Bridge contract build failed' }
    & $contractExe $hook
    if ($LASTEXITCODE -ne 0) { throw 'Bridge contract tests failed' }
} finally { Pop-Location }
