$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vc = Join-Path $vs 'VC\Auxiliary\Build\vcvars32.bat'
$command = 'call "{0}" >nul && cl /nologo /std:c++20 /EHsc /utf-8 /I"{1}\thirdparty" "{1}\tests\DesignWebViewSmoke.cpp" /Fe:"{1}\bin\tests\DesignWebViewSmoke.exe" /Fo:"{1}\bin\tests\DesignWebViewSmoke.obj" /link /LIBPATH:"{1}\thirdparty\webview2\x86" WebView2LoaderStatic.lib user32.lib ole32.lib oleaut32.lib version.lib shlwapi.lib advapi32.lib' -f $vc,$root
& cmd.exe /d /s /c $command
if ($LASTEXITCODE) { throw 'Design WebView smoke build failed' }
Push-Location (Join-Path $root 'bin/tests')
try { & .\DesignWebViewSmoke.exe (Join-Path $root 'tests/fixtures/source-canvas'); if ($LASTEXITCODE) { throw 'Design WebView smoke failed' } }
finally { Pop-Location }
