Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

$repoRoot = Get-RepoRoot
$environment = Initialize-DeveloperEnvironment $repoRoot
[void](Resolve-CorpusRoot $repoRoot)
Invoke-Client $repoRoot
