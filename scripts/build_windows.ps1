# Builds the VST3, CLAP and Standalone for Windows x64 and runs the tests.
# Needs Visual Studio 2022 with the C++ workload, CMake 3.25+ and Git.
$ErrorActionPreference = "Stop"
$projectDir = Split-Path -Parent $PSScriptRoot

cmake --preset windows-release -S $projectDir
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake --build --preset windows-release --target LetsComp_VST3 LetsComp_CLAP LetsComp_Standalone `
    LetsCompTests LetsCompAssistantTests LetsCompAssist LetsCompProcessorTests
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
ctest --preset windows-release
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$artefacts = Join-Path $projectDir "build/windows-release/LetsComp_artefacts/Release"
Write-Host "Build complete:"
Write-Host "  VST3: $artefacts/VST3/LetsComp.vst3"
Write-Host "  CLAP: $artefacts/CLAP/LetsComp.clap"
