$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectPath = Join-Path $projectRoot 'JadeDesigner.vcxproj'
$msBuild = $env:MSBUILD_EXE

if ($msBuild -and -not (Test-Path -LiteralPath $msBuild)) {
    $msBuild = $null
}

if (-not $msBuild) {
    $vsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vsWhere) {
        $msBuild = & $vsWhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
    }
}

if (-not $msBuild -or -not (Test-Path $msBuild)) {
    throw 'MSBuild.exe not found.'
}

& $msBuild $projectPath /t:Rebuild '/p:Configuration=Release;Platform=Win32' /m
if ($LASTEXITCODE -ne 0) {
    throw "Build failed with exit code $LASTEXITCODE."
}

$output = Join-Path $projectRoot 'bin\Release\JadeHybrid.fne'
if (-not (Test-Path $output)) {
    throw "Build reported success but output was not found: $output"
}

$webOutput = Join-Path $projectRoot 'bin\Release\JadeDesigner\web'
New-Item -ItemType Directory -Force -Path $webOutput | Out-Null
Copy-Item -Force (Join-Path $projectRoot 'web\index.html') (Join-Path $webOutput 'index.html')

Write-Host "Build succeeded: $output"
Write-Host "Preview files staged: $webOutput"

$hook = Join-Path (Split-Path -Parent $projectRoot) 'jadehook\bin\Release\jadehook.dll'
if (-not (Test-Path $hook)) {
    throw "jadehook.dll not found: $hook"
}
Copy-Item -Force $hook (Join-Path $projectRoot 'bin\Release\jadehook.dll')
Write-Host "Hook staged: $hook"
