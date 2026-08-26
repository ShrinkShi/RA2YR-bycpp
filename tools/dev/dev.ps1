param([switch]$Clean)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

$repoRoot = Get-RepoRoot
$environment = Initialize-DeveloperEnvironment $repoRoot

if ($Clean) { Remove-FailedBuildCache $repoRoot }
$cache = Join-Path (Get-BuildRoot $repoRoot) "CMakeCache.txt"
if ($Clean -or -not (Test-Path -LiteralPath $cache)) {
    Invoke-Configure $environment
} else {
    Write-Ok "Using existing CMake configure cache; dependencies are not reinstalled."
}
Invoke-Build $environment
Invoke-Tests $environment
Invoke-Client $repoRoot
