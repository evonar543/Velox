param(
  [string]$Version = "",
  [string]$BuildConfig = "Release"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$cmakeFile = Join-Path $repoRoot "CMakeLists.txt"

if (-not $Version) {
  $cmakeText = Get-Content $cmakeFile -Raw
  $match = [regex]::Match($cmakeText, "project\(Velox VERSION ([0-9]+\.[0-9]+\.[0-9]+)")
  if (-not $match.Success) {
    throw "Could not determine the Velox version from CMakeLists.txt."
  }

  $Version = $match.Groups[1].Value
}

$buildRoot = Join-Path $repoRoot "build"
$runtimeRoot = Join-Path $buildRoot $BuildConfig
$distRoot = Join-Path $repoRoot "dist"
$stageName = "velox-v$Version-windows-x64"
$stageRoot = Join-Path $distRoot $stageName
$zipPath = Join-Path $distRoot "$stageName.zip"

if (-not (Test-Path (Join-Path $runtimeRoot "velox.exe"))) {
  throw "Expected a built runtime at '$runtimeRoot'. Build Velox first."
}

New-Item -ItemType Directory -Force -Path $distRoot | Out-Null

if (Test-Path $stageRoot) {
  Remove-Item $stageRoot -Recurse -Force
}

if (Test-Path $zipPath) {
  Remove-Item $zipPath -Force
}

New-Item -ItemType Directory -Force -Path $stageRoot | Out-Null

$runtimeFiles = @(
  "velox.exe",
  "libcef.dll",
  "chrome_elf.dll",
  "d3dcompiler_47.dll",
  "dxcompiler.dll",
  "dxil.dll",
  "icudtl.dat",
  "libEGL.dll",
  "libGLESv2.dll",
  "resources.pak",
  "chrome_100_percent.pak",
  "chrome_200_percent.pak",
  "v8_context_snapshot.bin",
  "vk_swiftshader.dll",
  "vk_swiftshader_icd.json",
  "vulkan-1.dll"
)

$runtimeDirectories = @(
  "locales",
  "config",
  "extensions"
)

foreach ($file in $runtimeFiles) {
  $source = Join-Path $runtimeRoot $file
  if (-not (Test-Path $source)) {
    throw "Missing required runtime file '$file' in '$runtimeRoot'."
  }

  Copy-Item $source -Destination (Join-Path $stageRoot $file) -Force
}

foreach ($directory in $runtimeDirectories) {
  $source = Join-Path $runtimeRoot $directory
  if (-not (Test-Path $source)) {
    $fallback = Join-Path $repoRoot $directory
    if (Test-Path $fallback) {
      $source = $fallback
    } else {
      throw "Missing required runtime directory '$directory'."
    }
  }

  Copy-Item $source -Destination (Join-Path $stageRoot $directory) -Recurse -Force
}

Copy-Item (Join-Path $repoRoot "README.md") -Destination (Join-Path $stageRoot "README.md") -Force

$notesPath = Join-Path $distRoot "release-notes-v$Version.txt"
@"
Velox v$Version

This package contains the minimal Windows x64 runtime for Velox:
- velox.exe
- required CEF DLLs/resources
- config/settings.json
- extensions/sample-hello

Velox v0.4.0 and newer can load unpacked extensions from the bundled
extensions folder or from custom --extension-dir paths.

Unzip the folder and run velox.exe.
"@ | Set-Content -Path $notesPath -Encoding ascii

Compress-Archive -Path $stageRoot -DestinationPath $zipPath -Force

$zipInfo = Get-Item $zipPath
Write-Host "Created $($zipInfo.FullName) ($([math]::Round($zipInfo.Length / 1MB, 2)) MB)"
