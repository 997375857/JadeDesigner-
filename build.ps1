param([string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
if ($Configuration -ne 'Release') { throw 'This package is validated for Release x86 only.' }
$repo = $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$msbuild = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (!$msbuild) { throw 'MSBuild not found' }
Push-Location $repo
try {
    Push-Location (Join-Path $repo 'designer-tools')
    try {
        & node build.mjs
        if ($LASTEXITCODE) { throw 'Designer tools bundle failed; run npm ci in designer-tools first.' }
        & node --test tests/static-text.test.mjs tests/diagnostics.test.mjs tests/visual-model.test.mjs
        if ($LASTEXITCODE) { throw 'Designer text mapping tests failed' }
    } finally { Pop-Location }
    & $msbuild 'jadehook\jadehook.vcxproj' -p:Configuration=Release -p:Platform=Win32 -v:minimal -nologo
    if ($LASTEXITCODE) { throw 'Hook build failed' }
    & (Join-Path $repo 'tests\run-tests.ps1')
    & $msbuild 'JadeDesigner.sln' -p:Configuration=Release -p:Platform=x86 -v:minimal -nologo
    if ($LASTEXITCODE) { throw 'Plugin build failed' }
    Copy-Item -LiteralPath (Join-Path $repo 'jadehook\bin\Release\jadehook.dll') -Destination (Join-Path $repo 'bin\Release\jadehook.dll')
    Get-FileHash -LiteralPath (Join-Path $repo 'bin\Release\JadeHybrid.fne'),(Join-Path $repo 'bin\Release\jadehook.dll') -Algorithm SHA256
} finally { Pop-Location }
