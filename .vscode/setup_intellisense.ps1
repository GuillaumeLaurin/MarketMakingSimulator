#Requires -Version 5
<#
.SYNOPSIS
    Generate the local (git-ignored) VS Code configuration for MMS:
    c_cpp_properties.json, settings.json, tasks.json and launch.json.

    Everything is wired to a CMake preset's compile_commands.json so that every
    vcpkg dependency (fmt, Eigen, Catch2, ...) and every generated header
    (mms/version.hpp, <name>/<name>_export.hpp) resolves in IntelliSense.

.PARAMETER Preset
    CMake configure preset to bind to. Default: dev-mingw.
    Its build tree is build/<preset>/ and its compile database
    build/<preset>/compile_commands.json.

.PARAMETER SkipConfigure
    Do not run "cmake --preset <preset>"; only rewrite the JSON files.

.EXAMPLE
    .\.vscode\setup_intellisense.ps1
    .\.vscode\setup_intellisense.ps1 -Preset dev -SkipConfigure
#>
[CmdletBinding()]
param(
    [string] $Preset = "dev-mingw",
    [switch] $SkipConfigure
)

$ErrorActionPreference = "Stop"
$VsCodeDir = $PSScriptRoot
$RepoRoot  = (Resolve-Path (Join-Path $VsCodeDir "..")).Path
$BuildDir  = Join-Path $RepoRoot "build\$Preset"
$CompileDb = Join-Path $BuildDir "compile_commands.json"

function Info($m) { Write-Host "[vscode] $m" }
function ToSlash($p) { if ($p) { $p -replace '\\', '/' } else { "" } }

function Write-JsonFile($name, $content) {
    $path = Join-Path $VsCodeDir $name
    $text = ($content.TrimEnd() -replace "`r`n", "`n") + "`n"
    [System.IO.File]::WriteAllText($path, $text, (New-Object System.Text.UTF8Encoding $false))
    Info "wrote $name"
}

# --- locate vcpkg ---------------------------------------------------------------
$VcpkgRoot = $env:VCPKG_ROOT
if (-not $VcpkgRoot -or -not (Test-Path (Join-Path $VcpkgRoot "vcpkg.exe"))) {
    foreach ($cand in @("P:\vcpkg", "C:\vcpkg", (Join-Path $env:USERPROFILE "vcpkg"), "C:\dev\vcpkg")) {
        if (Test-Path (Join-Path $cand "vcpkg.exe")) { $VcpkgRoot = $cand; break }
    }
}
if ($VcpkgRoot) {
    $env:VCPKG_ROOT = $VcpkgRoot
    Info "vcpkg      : $VcpkgRoot"
} else {
    Write-Warning "VCPKG_ROOT not found. Set it before configuring or dependencies will not resolve."
}

# --- pick a compiler that matches the preset --------------------------------
$PreferGcc = $Preset -match 'mingw'

$Gxx = @(
    "C:\msys64\ucrt64\bin\g++.exe",
    "C:\msys64\mingw64\bin\g++.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1

$MsysBin = if ($Gxx) { Split-Path $Gxx -Parent } else { $null }

$Cl = $null
$VsWhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $VsWhere) {
    $vsPath = ([string](& $VsWhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath 2>$null)).Trim()
    if ($vsPath) {
        $msvcDir = Join-Path $vsPath "VC\Tools\MSVC"
        if (Test-Path $msvcDir) {
            $newest = Get-ChildItem $msvcDir -Directory | Sort-Object Name -Descending | Select-Object -First 1
            $candidate = Join-Path $newest.FullName "bin\Hostx64\x64\cl.exe"
            if (Test-Path $candidate) { $Cl = $candidate }
        }
    }
}

if ($PreferGcc -and $Gxx) {
    $CompilerPath = $Gxx; $Mode = "windows-gcc-x64"; $UsingGcc = $true
} elseif ($Cl) {
    $CompilerPath = $Cl;  $Mode = "windows-msvc-x64"; $UsingGcc = $false
} elseif ($Gxx) {
    $CompilerPath = $Gxx; $Mode = "windows-gcc-x64"; $UsingGcc = $true
} else {
    Write-Warning "No g++ or cl.exe found; leaving the IntelliSense compiler empty."
    $CompilerPath = ""; $Mode = "windows-gcc-x64"; $UsingGcc = $true
}
Info "compiler   : $CompilerPath ($Mode)"

# --- configure the preset so compile_commands.json exists -------------------
if (-not $SkipConfigure) {
    if (Get-Command cmake -ErrorAction SilentlyContinue) {
        Info "configure  : cmake --preset $Preset  (installs vcpkg deps, can take a while)"
        Push-Location $RepoRoot
        try { & cmake --preset $Preset } finally { Pop-Location }
    } else {
        Write-Warning "cmake is not on PATH; skipping configure."
    }
}
if (-not (Test-Path $CompileDb)) {
    Write-Warning "No compile database at $CompileDb."
    Write-Warning "Run 'cmake --preset $Preset' once; IntelliSense will use includePath until then."
}

# --- values spliced into the JSON templates below --------------------------
$compilerSlash = ToSlash $CompilerPath
$vcpkgSlash    = ToSlash $VcpkgRoot
$pathPrepend   = if ($UsingGcc -and $MsysBin) { (ToSlash $MsysBin) + ';${env:PATH}' } else { $null }
$gdbPath       = if ($MsysBin) { ToSlash (Join-Path $MsysBin "gdb.exe") } else { "gdb" }

# ==========================================================================
#  c_cpp_properties.json
# ==========================================================================
$cpp = @'
{
  "version": 4,
  "configurations": [
    {
      "name": "MMS",
      "compilerPath": "@@COMPILER@@",
      "compileCommands": "${workspaceFolder}/build/@@PRESET@@/compile_commands.json",
      "cStandard": "c17",
      "cppStandard": "c++20",
      "intelliSenseMode": "@@MODE@@",
      "includePath": [
        "${workspaceFolder}/include",
        "${workspaceFolder}/modules/*/include",
        "${workspaceFolder}/build/@@PRESET@@/**"
      ],
      "browse": {
        "path": ["${workspaceFolder}"],
        "limitSymbolsToIncludedHeaders": true
      }
    }
  ]
}
'@
$cpp = $cpp.Replace('@@COMPILER@@', $compilerSlash).Replace('@@PRESET@@', $Preset).Replace('@@MODE@@', $Mode)
Write-JsonFile "c_cpp_properties.json" $cpp

# ==========================================================================
#  settings.json
# ==========================================================================
if ($pathPrepend) {
    $settingsTail = @"
,
  "cmake.environment": {
    "VCPKG_ROOT": "$vcpkgSlash",
    "PATH": "$pathPrepend"
  },
  "terminal.integrated.env.windows": {
    "PATH": "$pathPrepend"
  }
"@
} elseif ($VcpkgRoot) {
    $settingsTail = @"
,
  "cmake.environment": {
    "VCPKG_ROOT": "$vcpkgSlash"
  }
"@
} else {
    $settingsTail = ""
}

$settings = @'
{
  "cmake.useCMakePresets": "always",
  "cmake.configureOnOpen": true,
  "cmake.defaultConfigurePreset": "@@PRESET@@",
  "C_Cpp.default.configurationProvider": "ms-vscode.cmake-tools",
  "C_Cpp.default.compileCommands": "${workspaceFolder}/build/@@PRESET@@/compile_commands.json",
  "C_Cpp.default.cppStandard": "c++20",
  "C_Cpp.autoAddFileAssociations": false,
  "files.associations": {
    "*.in": "cmake",
    "*.cmake.in": "cmake"
  },
  "editor.formatOnSave": true,
  "[cpp]": { "editor.defaultFormatter": "ms-vscode.cpptools" },
  "[c]": { "editor.defaultFormatter": "ms-vscode.cpptools" }@@TAIL@@
}
'@
$settings = $settings.Replace('@@PRESET@@', $Preset).Replace('@@TAIL@@', $settingsTail)
Write-JsonFile "settings.json" $settings

# ==========================================================================
#  tasks.json
# ==========================================================================
if ($pathPrepend) {
    $taskEnv = @"
,
        "env": { "PATH": "$pathPrepend" }
"@
} else {
    $taskEnv = ""
}

$tasks = @'
{
  "version": "2.0.0",
  "tasks": [
    {
      "label": "CMake configure (@@PRESET@@)",
      "type": "shell",
      "command": "cmake",
      "args": ["--preset", "@@PRESET@@"],
      "options": {
        "cwd": "${workspaceFolder}"@@ENV@@
      },
      "problemMatcher": []
    },
    {
      "label": "CMake build (@@PRESET@@)",
      "type": "shell",
      "command": "cmake",
      "args": ["--build", "--preset", "@@PRESET@@"],
      "options": {
        "cwd": "${workspaceFolder}"@@ENV@@
      },
      "group": { "kind": "build", "isDefault": true },
      "problemMatcher": ["$gcc"]
    },
    {
      "label": "CTest (@@PRESET@@)",
      "type": "shell",
      "command": "ctest",
      "args": ["--preset", "@@PRESET@@"],
      "options": {
        "cwd": "${workspaceFolder}"@@ENV@@
      },
      "group": { "kind": "test", "isDefault": true },
      "problemMatcher": []
    }
  ]
}
'@
$tasks = $tasks.Replace('@@PRESET@@', $Preset).Replace('@@ENV@@', $taskEnv)
Write-JsonFile "tasks.json" $tasks

# ==========================================================================
#  launch.json
# ==========================================================================
if ($UsingGcc) {
    $launch = @'
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "Debug mms (gdb)",
      "type": "cppdbg",
      "request": "launch",
      "program": "${workspaceFolder}/build/@@PRESET@@/out/bin/mms.exe",
      "args": [],
      "stopAtEntry": false,
      "cwd": "${workspaceFolder}",
      "externalConsole": false,
      "MIMode": "gdb",
      "miDebuggerPath": "@@GDB@@",
      "preLaunchTask": "CMake build (@@PRESET@@)",
      "environment": [{ "name": "PATH", "value": "@@PATH@@" }],
      "setupCommands": [
        { "description": "Enable pretty-printing", "text": "-enable-pretty-printing", "ignoreFailures": true }
      ]
    }
  ]
}
'@
    $launch = $launch.Replace('@@GDB@@', $gdbPath).Replace('@@PATH@@', $pathPrepend)
} else {
    $launch = @'
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "Debug mms (MSVC)",
      "type": "cppvsdbg",
      "request": "launch",
      "program": "${workspaceFolder}/build/@@PRESET@@/out/bin/mms.exe",
      "args": [],
      "stopAtEntry": false,
      "cwd": "${workspaceFolder}",
      "console": "integratedTerminal",
      "preLaunchTask": "CMake build (@@PRESET@@)"
    }
  ]
}
'@
}
$launch = $launch.Replace('@@PRESET@@', $Preset)
Write-JsonFile "launch.json" $launch

Write-Host ""
Write-Host "Done. Reload VS Code: Ctrl+Shift+P > 'Developer: Reload Window'" -ForegroundColor Green
