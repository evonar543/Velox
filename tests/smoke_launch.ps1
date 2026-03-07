param(
  [string]$Executable = ".\\build\\Release\\velox.exe",
  [string]$Url = "https://example.com",
  [string]$Metrics = ".\\build\\Release\\logs\\smoke-metrics.jsonl"
)

if (-not (Test-Path $Executable)) {
  throw "Executable not found: $Executable"
}

$runtimeRoot = Split-Path -Parent (Resolve-Path $Executable)
$sampleExtensionManifest = Join-Path $runtimeRoot "extensions\\sample-hello\\manifest.json"
if (-not (Test-Path $sampleExtensionManifest)) {
  throw "Expected staged sample extension at $sampleExtensionManifest"
}

$process = Start-Process `
  -FilePath $Executable `
  -ArgumentList "--url=$Url", "--quit-after-load", "--dump-benchmarks=$Metrics" `
  -PassThru `
  -Wait

if ($process.ExitCode -ne 0) {
  throw "Velox exited with code $($process.ExitCode)"
}

if (-not (Test-Path $Metrics)) {
  throw "Benchmark file was not created: $Metrics"
}

Write-Host "Smoke test completed successfully."
