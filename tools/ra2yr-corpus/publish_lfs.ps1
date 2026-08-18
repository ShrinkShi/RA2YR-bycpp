param(
    [Parameter(Mandatory = $true)]
    [string]$SourceDir,

    [string]$Branch = "agent/ra2yr-corpus-lfs"
)

$ErrorActionPreference = "Stop"

function Require-Command([string]$Name) {
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Required command not found: $Name"
    }
}

Require-Command git
Require-Command py

try {
    git lfs version | Out-Null
} catch {
    throw "Git LFS is not installed. Install Git LFS first, then rerun this script."
}

$RepoRoot = (git rev-parse --show-toplevel).Trim()
if (-not $RepoRoot) {
    throw "Run this script inside the RA2YR-bycpp git repository."
}
Set-Location $RepoRoot

$Remote = (git remote get-url origin).Trim()
if ($Remote -notmatch "ShrinkShi/RA2YR-bycpp") {
    throw "Unexpected origin remote: $Remote"
}

if ((git status --porcelain)) {
    throw "Working tree is not clean. Commit or stash unrelated work before importing the corpus."
}

git fetch origin main
git switch main
git pull --ff-only origin main

if ((git branch --list $Branch).Trim()) {
    git branch -D $Branch
}
git switch -c $Branch

git lfs install --local

$Builder = Join-Path $RepoRoot "tools/ra2yr-corpus/build_corpus.py"
py $Builder --source $SourceDir --repo-root $RepoRoot

git add .gitattributes
git add "尤里的复仇1.001/corpus"

$BinaryPatterns = @(
    "尤里的复仇1.001/corpus/**/*.mix",
    "尤里的复仇1.001/corpus/**/*.MIX",
    "尤里的复仇1.001/corpus/**/*.yro",
    "尤里的复仇1.001/corpus/**/*.map",
    "尤里的复仇1.001/corpus/**/*.shp",
    "尤里的复仇1.001/corpus/**/*.vxl",
    "尤里的复仇1.001/corpus/**/*.hva",
    "尤里的复仇1.001/corpus/**/*.tmp",
    "尤里的复仇1.001/corpus/**/*.pal",
    "尤里的复仇1.001/corpus/**/*.csf"
)

$TrackedBinaryFiles = foreach ($Pattern in $BinaryPatterns) {
    git ls-files $Pattern
}

$NonLfs = foreach ($Path in $TrackedBinaryFiles) {
    $Attr = git check-attr filter -- $Path
    if ($Attr -notmatch "filter: lfs$") {
        $Path
    }
}
if ($NonLfs) {
    throw "Some corpus binary files are not tracked by Git LFS:`n$($NonLfs -join "`n")"
}

Write-Host "----- git lfs status -----"
git lfs status

$Manifest = Get-Content "尤里的复仇1.001/corpus/corpus-manifest.json" -Raw | ConvertFrom-Json
Write-Host ("Entries: {0}; payload: {1:N3} GiB" -f $Manifest.entryCount, ($Manifest.totalBytes / 1GB))

if ($Manifest.entryCount -ne 126) {
    throw "Unexpected corpus entry count: $($Manifest.entryCount); expected 126 for the audited sample."
}
if ($Manifest.totalBytes -ne 1269875373) {
    throw "Unexpected corpus payload size: $($Manifest.totalBytes); expected 1269875373 bytes for the audited sample."
}

git commit -m "assets: add RA2YR 1.001 compatibility corpus via Git LFS"

# Git LFS uploads binary objects before Git updates the remote branch ref.
git push -u origin $Branch

Write-Host "Corpus branch pushed: $Branch"
Write-Host "Verify a fresh clone can materialize all LFS objects before merging into main."
