$ErrorActionPreference = 'Stop'
$prototypeRoot = $PSScriptRoot
# JUCE's console build helper expects UTF-8 argv. Use the same directory's
# Windows short alias to avoid corrupting Cyrillic paths in the helper's CRT.
if ($prototypeRoot -match '[^\x00-\x7F]') {
    $fileSystem = New-Object -ComObject Scripting.FileSystemObject
    $prototypeRoot = $fileSystem.GetFolder($prototypeRoot).ShortPath
    if ($prototypeRoot -match '[^\x00-\x7F]') {
        throw 'The build helper needs an ASCII path. Enable an ASCII directory alias or build from an ASCII checkout.'
    }
}
python -c "import mido"
if ($LASTEXITCODE -ne 0) { throw 'Install Python dependencies: python -m pip install -r prototype/requirements.txt' }
cmake -S $prototypeRoot -B "$prototypeRoot/build" -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed' }
cmake --build "$prototypeRoot/build" --config Release --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
Write-Host 'Ready: app/ICEPUNK.exe. Double-click Launch ICEPUNK.vbs in the project folder.'
