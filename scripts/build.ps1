param([switch]$Run, [switch]$TestOnly)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot
try {
    $compiler = Join-Path $projectRoot 'local/w64devkit/bin/g++.exe'
    if (Test-Path -LiteralPath $compiler) {
        New-Item -ItemType Directory -Force build | Out-Null
        & $compiler -std=c++17 -O2 -Wall -Wextra -Wpedantic -static -Isrc src/simulation.cpp src/electrosense.cpp tests/core_tests.cpp -o build/ghostwave_tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Test compilation failed.' }
        & ./build/ghostwave_tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Core tests failed.' }
        & $compiler -std=c++17 -O2 -Wall -Wextra -Wpedantic -static -Isrc src/electrosense.cpp src/simulation.cpp tests/electrosense_tests.cpp -o build/electrosense_tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Electrosense test compilation failed.' }
        & ./build/electrosense_tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Electrosense tests failed.' }
        & $compiler -std=c++17 -O2 -Wall -Wextra -Wpedantic -static -Isrc -Itests/audio_support src/simulation.cpp src/electrosense.cpp tests/audio_tests.cpp -o build/audio_tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Audio test compilation failed.' }
        & ./build/audio_tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Audio tests failed.' }
        if (!$TestOnly) {
            $raylib = Get-ChildItem -LiteralPath local/raylib -Directory | Select-Object -First 1
            & $compiler -std=c++17 -O2 -Wall -Wextra -Wpedantic -static -Isrc "-I$($raylib.FullName)/include" src/main.cpp src/simulation.cpp src/electrosense.cpp "$($raylib.FullName)/lib/libraylib.a" -lopengl32 -lgdi32 -lwinmm -o build/ghostwave.exe
            if ($LASTEXITCODE -ne 0) { throw 'Game compilation failed.' }
            Copy-Item -Recurse -Force assets,config -Destination build
        }
    } else {
        $game = if ($TestOnly) { 'OFF' } else { 'ON' }
        cmake -S . -B build "-DGHOSTWAVE_BUILD_GAME=$game" -DBUILD_TESTING=ON
        if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed. Install CMake and a C++ compiler.' }
        cmake --build build --config Release --parallel
        if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
        ctest --test-dir build -C Release --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
    }
    if ($Run -and !$TestOnly) {
        $gameExe = if (Test-Path build/ghostwave.exe) { 'build/ghostwave.exe' } else { 'build/Release/ghostwave.exe' }
        & $gameExe
    }
} finally { Pop-Location }
