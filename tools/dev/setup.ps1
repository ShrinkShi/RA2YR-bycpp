param([switch]$Clean)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

$repoRoot = Get-RepoRoot
$environment = Initialize-DeveloperEnvironment $repoRoot

if ($Clean) { Remove-FailedBuildCache $repoRoot }
Invoke-Configure $environment
Write-Ok "CMake configure completed."
