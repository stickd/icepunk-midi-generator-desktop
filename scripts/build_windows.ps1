param([string]$JuceSource = '', [string]$InnoCompiler = '', [string]$Vst3Path = '', [switch]$SkipTests, [string]$LicenseOrigin = '', [string]$LicensePublicKey = '', [string]$LicenseFile = '')
$ErrorActionPreference = 'Stop'
if ($LicenseOrigin -notmatch '^https://[a-zA-Z0-9.-]+(:[0-9]+)?$' -or $LicensePublicKey -notmatch '^[0-9a-f]{64}$') { throw 'Supply -LicenseOrigin HTTPS origin and -LicensePublicKey Ed25519 public key.' }
if (-not $SkipTests -and -not (Test-Path -LiteralPath $LicenseFile -PathType Leaf)) { throw 'Supply -LicenseFile with a valid signed authorization for this build machine to run packaged tests.' }
$releaseRoot = Split-Path -Parent $PSScriptRoot
$version = (Get-Content -LiteralPath (Join-Path $releaseRoot 'VERSION') -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw 'VERSION must contain major.minor.patch' }
function Run-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
function Reset-OwnedDirectory([string]$Path, [string]$Parent) {
    $full = [IO.Path]::GetFullPath($Path)
    $allowed = [IO.Path]::GetFullPath($Parent).TrimEnd('\') + '\'
    if (-not $full.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) { throw "Unsafe cleanup path: $full" }
    if (Test-Path -LiteralPath $full) {
        if (-not (Test-Path -LiteralPath (Join-Path $full '.icepunk-build'))) { throw "Refusing to clear unmarked directory: $full" }
        Remove-Item -LiteralPath $full -Recurse -Force
    }
    New-Item -ItemType Directory -Path $full -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $full '.icepunk-build') -Value 'Owned release staging' -Encoding ascii
}
$tools = Join-Path $releaseRoot '.release-tools'
$python = Join-Path $tools 'Scripts\python.exe'
if (-not (Test-Path -LiteralPath $python)) { Run-Checked 'python' @('-m','venv',$tools) }
Run-Checked $python @('-m','pip','install','-r',(Join-Path $releaseRoot 'engine\requirements-build.txt'))
$work = Join-Path $releaseRoot '.release-build\windows'
New-Item -ItemType Directory -Force -Path $work | Out-Null
$engineDist = Join-Path $work 'engine-dist'
Reset-OwnedDirectory $engineDist $work
Run-Checked $python @('-m','PyInstaller','--clean','--noconfirm','--workpath',(Join-Path $work 'pyinstaller'),
    '--distpath',$engineDist,(Join-Path $releaseRoot 'engine\icepunk_engine.spec'))

# The JUCE command-line helper needs ASCII paths on Windows. No source relocation.
$cmakeRoot = $releaseRoot
if ($cmakeRoot -match '[^\x00-\x7F]') {
    $fs = New-Object -ComObject Scripting.FileSystemObject
    $cmakeRoot = $fs.GetFolder($releaseRoot).ShortPath
    if ($cmakeRoot -match '[^\x00-\x7F]') { throw 'Build from an ASCII path or enable a short directory alias.' }
}
$cmakeBuild = Join-Path $cmakeRoot '.release-build\windows\cmake-release-v2'
$configArgs = @('-S',(Join-Path $cmakeRoot 'prototype'),'-B',$cmakeBuild,'-G','Visual Studio 17 2022','-A','x64',
    '-DICEPUNK_DEVELOPMENT_ENGINE=OFF',"-DICEPUNK_LICENSE_ORIGIN=$LicenseOrigin","-DICEPUNK_LICENSE_PUBLIC_KEY=$LicensePublicKey")
if ($JuceSource) { $configArgs += "-DFETCHCONTENT_SOURCE_DIR_JUCE=$JuceSource" }
Run-Checked 'cmake' $configArgs
Run-Checked 'cmake' @('--build',$cmakeBuild,'--config','Release','--parallel','4')
$stageParent = Join-Path $releaseRoot 'dist\windows'
$stage = Join-Path $stageParent 'ICEPUNK MIDI Generator'
Reset-OwnedDirectory $stage $stageParent
Copy-Item -LiteralPath (Join-Path $cmakeBuild 'release-app\ICEPUNK.exe') -Destination $stage
Copy-Item -LiteralPath (Join-Path $engineDist 'icepunk-engine') -Destination (Join-Path $stage 'engine') -Recurse
if ($Vst3Path) {
    $vst = Get-Item -LiteralPath $Vst3Path
    if ($vst.Extension -ne '.vst3') { throw 'Vst3Path must refer to a real .vst3 file/bundle' }
    New-Item -ItemType Directory -Path (Join-Path $stage 'vst3') | Out-Null
    if ($vst.PSIsContainer) { Copy-Item -Path (Join-Path $vst.FullName '*') -Destination (Join-Path $stage 'vst3') -Recurse }
    else { throw 'This pipeline expects the standard Windows VST3 bundle directory.' }
}
$juceEntry = Select-String -LiteralPath (Join-Path $cmakeBuild 'CMakeCache.txt') -Pattern '^juce_SOURCE_DIR:STATIC=(.+)$'
if ($juceEntry) { $juceMetadataSource = $juceEntry.Matches[0].Groups[1].Value }
elseif ($JuceSource) { $juceMetadataSource = $JuceSource }
else { $juceMetadataSource = Join-Path $cmakeBuild '_deps\juce-src' }
Run-Checked $python @((Join-Path $releaseRoot 'scripts\release_metadata.py'),'--stage',$stage,'--juce',$juceMetadataSource)
if (-not $SkipTests) {
    Run-Checked $python @('-B','-m','unittest','discover','-s',(Join-Path $releaseRoot 'prototype\tests'),'-v')
    Run-Checked $python @((Join-Path $releaseRoot 'scripts\test_packaged.py'),'--stage',$stage,
        '--report',(Join-Path $work 'packaged-test.json'),'--license-file',$LicenseFile)
}
if ($env:ICEPUNK_SIGN_SCRIPT) {
    & $env:ICEPUNK_SIGN_SCRIPT $stage
    if (-not $?) { throw 'Release payload signing failed' }
}
if (-not $InnoCompiler) {
    $found = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($found) { $InnoCompiler = $found.Source }
    else {
        foreach ($base in @(${env:ProgramFiles(x86)},$env:ProgramFiles,(Join-Path $env:LOCALAPPDATA 'Programs'))) {
            foreach ($folder in @('Inno Setup 7','Inno Setup 6')) {
                $candidate = Join-Path $base "$folder\ISCC.exe"
                if (Test-Path -LiteralPath $candidate) { $InnoCompiler = $candidate; break }
            }
        }
    }
    if (-not $InnoCompiler) {
        foreach ($key in @('HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1',
                           'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 7_is1')) {
            $installed = Get-ItemProperty -LiteralPath $key -ErrorAction SilentlyContinue
            if ($installed.InstallLocation) {
                $candidate = Join-Path $installed.InstallLocation 'ISCC.exe'
                if (Test-Path -LiteralPath $candidate) { $InnoCompiler = $candidate; break }
            }
        }
    }
}
$dist = Join-Path $releaseRoot 'dist'
$installer = Join-Path $dist "ICEPUNK_MIDI_Generator_${version}_Windows_Setup.exe"
if ($InnoCompiler) {
    $innoArgs = @("/DAppVersion=$version","/DSourceDir=$stage","/DOutputDir=$dist")
    if ($env:ICEPUNK_INNO_SIGN_COMMAND) {
        $innoArgs += '/DSignInstaller=1'
        $innoArgs += "/Sicepunk=$env:ICEPUNK_INNO_SIGN_COMMAND"
    }
    if ($Vst3Path) { $innoArgs += '/DWithVST3=1' }
    $innoArgs += (Join-Path $releaseRoot 'installer\windows\ICEPUNK_MIDI_Generator.iss')
    Run-Checked $InnoCompiler $innoArgs
    if ($env:ICEPUNK_SIGN_SCRIPT) {
        & $env:ICEPUNK_SIGN_SCRIPT $installer
        if (-not $?) { throw 'Installer signing failed' }
    }
    Write-Host "Installer created: $installer"
} else {
    Write-Warning 'Inno Setup not found: payload is ready, but NO installer was compiled. Install Inno Setup and run again.'
}
$releaseSummary = @{ version=$version; payload=$stage; installerCompiled=[bool]$InnoCompiler; vst3Included=[bool]$Vst3Path;
    testsRun=(-not $SkipTests); signingHookUsed=[bool]$env:ICEPUNK_SIGN_SCRIPT }
$releaseSummary | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $work 'release-summary.json') -Encoding utf8
Write-Host "Windows payload: $stage"
