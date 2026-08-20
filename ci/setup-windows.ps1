#Requires -Version 5
# Ensure Ninja is available and a vcpkg checkout exists. The MSVC environment is
# provided by the workflow (ilammy/msvc-dev-cmd). Idempotent; safe to run locally.
$ErrorActionPreference = 'Stop'

if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) {
    Write-Host '[ci] installing ninja'
    choco install ninja -y --no-progress | Out-Null
}

if (-not $env:VCPKG_ROOT -or -not (Test-Path (Join-Path $env:VCPKG_ROOT 'vcpkg.exe'))) {
    Write-Host '[ci] bootstrapping vcpkg into .\vcpkg'
    if (-not (Test-Path .\vcpkg)) {
        git clone --depth 1 https://github.com/microsoft/vcpkg.git vcpkg
    }
    & .\vcpkg\bootstrap-vcpkg.bat -disableMetrics
    $root = (Resolve-Path .\vcpkg).Path
    $env:VCPKG_ROOT = $root
    if ($env:GITHUB_ENV) { "VCPKG_ROOT=$root" | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding utf8 }
}

Write-Host "[ci] cmake  : $((cmake --version | Select-Object -First 1))"
Write-Host "[ci] ninja  : $(ninja --version)"
Write-Host "[ci] vcpkg  : $env:VCPKG_ROOT"
