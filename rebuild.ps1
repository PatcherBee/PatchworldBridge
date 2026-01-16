# rebuild.ps1
Write-Host "Stopping Patchworld Bridge..." -ForegroundColor Yellow
Stop-Process -Name "Patchworld Bridge" -ErrorAction SilentlyContinue

# Ensure we are in the right directory
Set-Location "C:\PatchworldBridge"

# Nuke the old build folder to prevent cache errors
if (Test-Path "build_ninja") {
    Remove-Item -Path "build_ninja" -Recurse -Force
}

# Run CMake - Use "." to point to the current directory as source
cmake -G "Ninja" -S . -B build_ninja -DCMAKE_BUILD_TYPE=Release

if ($LASTEXITCODE -eq 0) {
    cmake --build build_ninja --config Release
    Write-Host "Build Successful!" -ForegroundColor Green
} else {
    Write-Host "Build Failed at Configuration Stage" -ForegroundColor Red
}