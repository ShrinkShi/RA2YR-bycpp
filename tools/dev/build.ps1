Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

$repoRoot = Get-RepoRoot
$environment = Initialize-DeveloperEnvironment $repoRoot
[void](Resolve-CorpusRoot $repoRoot)
$cache = Join-Path (Get-BuildRoot $repoRoot) "CMakeCache.txt"
if (-not (Test-Path -LiteralPath $cache)) {
    Invoke-Configure $environment
}
Invoke-Build $environment
Write-Ok "Build completed."
