param(
    [string]$Remote = "origin",
    [switch]$NoStash,
    [switch]$IncludeIgnored,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

function Invoke-Git {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Args
    )

    Write-Host ("+ git " + ($Args -join " "))
    if ($DryRun) {
        return ""
    }

    $output = & git @Args 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw ($output | Out-String)
    }

    return ($output | Out-String).TrimEnd()
}

function Get-GitOutput {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Args
    )

    $output = & git @Args 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw ($output | Out-String)
    }

    return ($output | Out-String).TrimEnd()
}

$repoRoot = Get-GitOutput -Args @("rev-parse", "--show-toplevel")
Set-Location $repoRoot

$branch = Get-GitOutput -Args @("rev-parse", "--abbrev-ref", "HEAD")
if ($branch -eq "HEAD") {
    throw "Detached HEAD is not supported. Please switch to a branch first."
}

$status = Get-GitOutput -Args @("status", "--porcelain=v1")
$upstream = ""

try {
    $upstream = Get-GitOutput -Args @("rev-parse", "--abbrev-ref", "--symbolic-full-name", "@{u}")
} catch {
    $upstream = "$Remote/$branch"
}

$stashCreated = $false
$stashMessage = "pre-pull backup $branch $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"

if ($status) {
    if ($NoStash) {
        throw "Working tree is dirty. Re-run without -NoStash or clean it manually."
    }

    $stashOutput = Invoke-Git -Args @("stash", "push", "--include-untracked", "-m", $stashMessage)
    if ($stashOutput -notmatch "No local changes to save") {
        $stashCreated = $true
    }
} else {
    Write-Host "Working tree already clean."
}

$cleanArgs = @("clean", "-fdX")
if ($IncludeIgnored) {
    $cleanArgs = @("clean", "-fdx")
}

Invoke-Git -Args $cleanArgs | Out-Null
Invoke-Git -Args @("fetch", "--prune", $Remote) | Out-Null
Invoke-Git -Args @("merge", "--ff-only", $upstream) | Out-Null

Write-Host ""
Write-Host "Sync complete."
Write-Host ("Branch: " + $branch)
Write-Host ("Upstream: " + $upstream)

if ($stashCreated) {
    Write-Host ("Backup stash: " + $stashMessage)
    Write-Host "Use `git stash list` to inspect it, and `git stash pop` when you want it back."
}
