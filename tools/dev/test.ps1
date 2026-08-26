Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

$repoRoot = Get-RepoRoot
$environment = Initialize-DeveloperEnvironment $repoRoot
if (-not (Test-Path -LiteralPath (Join-Path (Get-BuildRoot $repoRoot) "CTestTestfile.cmake"))) {
    throw "CTest configuration is missing. Run .\tools\dev\setup.ps1 and build first."
}
Invoke-Tests $environment
Write-Ok "CTest completed."
