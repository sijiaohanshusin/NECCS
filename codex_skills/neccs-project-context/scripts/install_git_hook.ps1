param(
    [string]$RepoRoot = (Get-Location).Path,
    [string]$SkillRoot = (Join-Path $PSScriptRoot "..")
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $RepoRoot)) {
    throw "RepoRoot does not exist: $RepoRoot"
}
if (-not (Test-Path -LiteralPath $SkillRoot)) {
    throw "SkillRoot does not exist: $SkillRoot"
}

$repo = [System.IO.Path]::GetFullPath($RepoRoot)
$skill = [System.IO.Path]::GetFullPath($SkillRoot)
$refreshScript = Join-Path $skill "scripts/refresh_context.ps1"
$outputDir = Join-Path $skill "references"

if (-not (Test-Path -LiteralPath $refreshScript)) {
    throw "Missing refresh script: $refreshScript"
}

$isRepo = (git -C $repo rev-parse --is-inside-work-tree 2>$null)
if ($LASTEXITCODE -ne 0 -or $isRepo.Trim() -ne "true") {
    throw "Not a git repository: $repo"
}

$gitDirRaw = (git -C $repo rev-parse --git-dir).Trim()
if ([string]::IsNullOrWhiteSpace($gitDirRaw)) {
    throw "Unable to resolve .git directory"
}
$gitDir = $gitDirRaw
if (-not [System.IO.Path]::IsPathRooted($gitDir)) {
    $gitDir = Join-Path $repo $gitDir
}
$hooksDir = Join-Path $gitDir "hooks"
if (-not (Test-Path -LiteralPath $hooksDir)) {
    New-Item -ItemType Directory -Path $hooksDir -Force | Out-Null
}

$refreshEscaped = $refreshScript.Replace("\", "/")
$repoEscaped = $repo.Replace("\", "/")
$outEscaped = $outputDir.Replace("\", "/")

$hookContent = @(
    "#!/bin/sh"
    "powershell -NoProfile -ExecutionPolicy Bypass -File ""$refreshEscaped"" -RepoRoot ""$repoEscaped"" -OutputDir ""$outEscaped"" > /dev/null 2>&1 || true"
) -join "`n"

$postCommitPath = Join-Path $hooksDir "post-commit"
$postMergePath = Join-Path $hooksDir "post-merge"

Set-Content -LiteralPath $postCommitPath -Value $hookContent -Encoding ASCII
Set-Content -LiteralPath $postMergePath -Value $hookContent -Encoding ASCII

& $refreshScript -RepoRoot $repo -OutputDir $outputDir

Write-Host "Installed hooks:"
Write-Host " - $postCommitPath"
Write-Host " - $postMergePath"
