# 1. Kill any running instances to avoid "Access Denied" errors
Write-Host "--- Stopping running processes ---" -ForegroundColor Cyan
Get-Process "Patchworld MIDI OSC Bridge" -ErrorAction SilentlyContinue | Stop-Process -Force

# 2. Configuration
$buildDir = "build_ninja"
$generator = "Ninja"

# 3. Clean and Reset
if (Test-Path $buildDir) {
    Write-Host "--- Cleaning build directory ---" -ForegroundColor Yellow
    Remove-Item -Recurse -Force $buildDir
}
New-Item -ItemType Directory -Path $buildDir | Out-Null

# 4. Configure with Ninja
# Note: We explicitly set the build type here because Ninja is a single-config generator
Write-Host "--- Configuring CMake with Ninja ---" -ForegroundColor Cyan
cmake -G $generator -B $buildDir -DCMAKE_BUILD_TYPE=Release

if ($LASTEXITCODE -ne 0) {
    Write-Host "!!! Configuration Failed. Are you in the Developer PowerShell? !!!" -ForegroundColor Red
    exit $LASTEXITCODE
}

# 5. Build
Write-Host "--- Building with Ninja ---" -ForegroundColor Cyan
cmake --build $buildDir --config Release --parallel

if ($LASTEXITCODE -eq 0) {
    Write-Host "`n--- Build Successful! ---" -ForegroundColor Green
    Write-Host "Output: $buildDir/PatchworldBridge_artefacts/Release/Patchworld MIDI OSC Bridge.exe"
} else {
    Write-Host "`n!!! Build Failed !!!" -ForegroundColor Red
}