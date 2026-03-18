param(
    [string]$RepoRoot = (Get-Location).Path,
    [string]$OutputDir = (Join-Path $PSScriptRoot "..\references")
)

$ErrorActionPreference = "Stop"

function Get-MacroValue {
    param(
        [string]$FilePath,
        [string]$MacroName
    )

    if (-not (Test-Path -LiteralPath $FilePath)) {
        return $null
    }

    $match = Select-String -LiteralPath $FilePath -Pattern "^\s*#define\s+$MacroName\s+(.+)$" | Select-Object -First 1
    if ($null -eq $match) {
        return $null
    }

    $line = $match.Line
    $line = [regex]::Replace($line, "/\*.*\*/", "")
    $line = [regex]::Replace($line, "//.*$", "")
    $lineMatch = [regex]::Match($line, "^\s*#define\s+$MacroName\s+(.+)$")
    if (-not $lineMatch.Success) {
        return $null
    }

    return $lineMatch.Groups[1].Value.Trim()
}

function Ensure-Dir {
    param([string]$PathToCreate)
    if (-not (Test-Path -LiteralPath $PathToCreate)) {
        New-Item -ItemType Directory -Path $PathToCreate -Force | Out-Null
    }
}

if (-not (Test-Path -LiteralPath $RepoRoot)) {
    throw "RepoRoot does not exist: $RepoRoot"
}

$repo = [System.IO.Path]::GetFullPath($RepoRoot)
$outDir = [System.IO.Path]::GetFullPath($OutputDir)
Ensure-Dir -PathToCreate $outDir

$gitInside = (git -C $repo rev-parse --is-inside-work-tree 2>$null)
if ($LASTEXITCODE -ne 0 -or $gitInside.Trim() -ne "true") {
    throw "Not a git repository: $repo"
}

$generatedAt = (Get-Date).ToString("yyyy-MM-ddTHH:mm:ssK")
$branch = (git -C $repo rev-parse --abbrev-ref HEAD).Trim()
$headSha = (git -C $repo rev-parse HEAD).Trim()
$headSubject = (git -C $repo log -1 --pretty=%s).Trim()
$headDate = (git -C $repo log -1 --date=iso-strict --pretty=%cd).Trim()
$statusLines = @(git -C $repo status --porcelain)
$recentCommits = @(git -C $repo log --oneline -n 8)

$mainC = Join-Path $repo "START/Core/Src/main.c"
$mpuC = Join-Path $repo "START/Core/Src/mpu.c"
$taskC = Join-Path $repo "START/User/App/app_main_task.c"
$taskH = Join-Path $repo "START/User/App/app_main_task.h"
$displayC = Join-Path $repo "START/User/App/app_display.c"
$displayH = Join-Path $repo "START/User/App/app_display.h"
$displayCfg = Join-Path $repo "START/User/App/app_display_cfg.h"
$dataOutC = Join-Path $repo "START/User/App/app_data_output.c"

$moduleList = @(
    [pscustomobject]@{ Module = "Boot and platform init"; Path = "START/Core/Src/main.c"; Note = "HAL init, MPU/cache, clocks, peripheral setup." },
    [pscustomobject]@{ Module = "MPU memory layout"; Path = "START/Core/Src/mpu.c"; Note = "DMA-safe vs cacheable memory regions." },
    [pscustomobject]@{ Module = "Main RTOS tasks"; Path = "START/User/App/app_main_task.c"; Note = "Audio pipeline and UI loop, runtime knobs, CLI." },
    [pscustomobject]@{ Module = "Task API and perf enum"; Path = "START/User/App/app_main_task.h"; Note = "Public handles/counters and perf section ids." },
    [pscustomobject]@{ Module = "Display render pipeline"; Path = "START/User/App/app_display.c"; Note = "Field prep, norm, upsample, overlay, commit." },
    [pscustomobject]@{ Module = "Display runtime config API"; Path = "START/User/App/app_display.h"; Note = "Modes, interpolation, normalization config." },
    [pscustomobject]@{ Module = "Display compile-time knobs"; Path = "START/User/App/app_display_cfg.h"; Note = "Field size, smoothing, dynamic range, defaults." },
    [pscustomobject]@{ Module = "UART/VOFA output"; Path = "START/User/App/app_data_output.c"; Note = "Data streaming and baud assumptions." }
)
$modules = foreach ($m in $moduleList) {
    $full = Join-Path $repo $m.Path
    if (Test-Path -LiteralPath $full) { $m }
}

$knobs = [ordered]@{
    UI_FPS_MIN                    = Get-MacroValue -FilePath $taskC -MacroName "UI_FPS_MIN"
    UI_FPS_MAX                    = Get-MacroValue -FilePath $taskC -MacroName "UI_FPS_MAX"
    UI_FPS_DEFAULT                = Get-MacroValue -FilePath $taskC -MacroName "UI_FPS_DEFAULT"
    AUDIO_ALGO_DECIM_DEFAULT      = Get-MacroValue -FilePath $taskC -MacroName "AUDIO_ALGO_DECIM_DEFAULT"
    APP_DISPLAY_FIELD_W           = Get-MacroValue -FilePath $displayCfg -MacroName "APP_DISPLAY_FIELD_W"
    APP_DISPLAY_FIELD_H           = Get-MacroValue -FilePath $displayCfg -MacroName "APP_DISPLAY_FIELD_H"
    APP_DISPLAY_DYNAMIC_DB_FLOOR  = Get-MacroValue -FilePath $displayCfg -MacroName "APP_DISPLAY_DYNAMIC_DB_FLOOR"
    APP_DISPLAY_DYNAMIC_GAMMA     = Get-MacroValue -FilePath $displayCfg -MacroName "APP_DISPLAY_DYNAMIC_GAMMA"
    APP_DISPLAY_BILINEAR_SAMPLING = Get-MacroValue -FilePath $displayCfg -MacroName "APP_DISPLAY_BILINEAR_SAMPLING"
}

$taskNames = @()
if (Test-Path -LiteralPath $taskC) {
    $taskLines = Select-String -LiteralPath $taskC -Pattern "xTaskCreate\(" -ErrorAction SilentlyContinue
    foreach ($line in $taskLines) {
        $m = [regex]::Match($line.Line, "xTaskCreate\([^,]+,\s*""([^""]+)""")
        if ($m.Success) {
            $taskNames += $m.Groups[1].Value
        }
    }
    $taskNames = $taskNames | Sort-Object -Unique
}

$queueNames = @()
if (Test-Path -LiteralPath $taskH) {
    $queueLines = Select-String -LiteralPath $taskH -Pattern "extern\s+QueueHandle_t\s+\w+\s*;" -ErrorAction SilentlyContinue
    foreach ($line in $queueLines) {
        $m = [regex]::Match($line.Line, "extern\s+QueueHandle_t\s+(\w+)\s*;")
        if ($m.Success) {
            $queueNames += $m.Groups[1].Value
        }
    }
    $queueNames = $queueNames | Sort-Object -Unique
}

$cliCommands = @()
if (Test-Path -LiteralPath $taskC) {
    $cliLines = Select-String -LiteralPath $taskC -Pattern "printf\(""cfg " -ErrorAction SilentlyContinue
    foreach ($line in $cliLines) {
        $m = [regex]::Match($line.Line, "printf\(""([^""]+)")
        if ($m.Success) {
            $cmd = $m.Groups[1].Value
            $cmd = $cmd.Replace("\r\n", "").Trim()
            if ($cmd -match "^cfg\s+" -and $cmd -notmatch "%") {
                $cliCommands += $cmd
            }
        }
    }
    $cliCommands = $cliCommands | Sort-Object -Unique
}

$perfSections = @()
if (Test-Path -LiteralPath $taskH) {
    $perfLines = Select-String -LiteralPath $taskH -Pattern "APP_PERF_SEC_[A-Z0-9_]+" -ErrorAction SilentlyContinue
    foreach ($line in $perfLines) {
        $m = [regex]::Match($line.Line, "APP_PERF_SEC_[A-Z0-9_]+")
        if ($m.Success -and $m.Value -ne "APP_PERF_SEC_COUNT") {
            $perfSections += $m.Value
        }
    }
    $perfSections = $perfSections | Sort-Object -Unique
}

$manualPath = Join-Path $outDir "project_manual.md"
$manualExists = Test-Path -LiteralPath $manualPath

$markdown = New-Object System.Collections.Generic.List[string]
$markdown.Add("# NECCS Project Snapshot")
$markdown.Add("")
$markdown.Add("- generated_at: $generatedAt")
$markdown.Add("- repo_root: $repo")
$markdown.Add("- git_branch: $branch")
$markdown.Add("- git_head: $headSha")
$markdown.Add("")
$markdown.Add("## Git Summary")
$markdown.Add("")
$markdown.Add("- latest_commit: $headSubject")
$markdown.Add("- latest_commit_date: $headDate")
$markdown.Add("- working_tree_dirty_files: $($statusLines.Count)")
$markdown.Add("")
$markdown.Add("### Recent Commits")
$markdown.Add("")
if ($recentCommits.Count -gt 0) {
    foreach ($line in $recentCommits) {
        $markdown.Add("- $line")
    }
} else {
    $markdown.Add("- (none)")
}
$markdown.Add("")
$markdown.Add("## Key Modules")
$markdown.Add("")
$markdown.Add("| Module | Path | Note |")
$markdown.Add("| --- | --- | --- |")
foreach ($m in $modules) {
    $markdown.Add('| ' + $m.Module + ' | `' + $m.Path + '` | ' + $m.Note + ' |')
}
$markdown.Add("")
$markdown.Add("## Runtime Knobs")
$markdown.Add("")
foreach ($k in $knobs.Keys) {
    $value = $knobs[$k]
    if ([string]::IsNullOrWhiteSpace([string]$value)) {
        $value = "(not found)"
    }
    $markdown.Add("- ${k}: $value")
}
$markdown.Add("")
$markdown.Add("## RTOS Objects")
$markdown.Add("")
$markdown.Add("- tasks:")
if ($taskNames.Count -gt 0) {
    foreach ($taskName in $taskNames) {
        $markdown.Add("  - $taskName")
    }
} else {
    $markdown.Add("  - (none)")
}
$markdown.Add("- queues:")
if ($queueNames.Count -gt 0) {
    foreach ($queue in $queueNames) {
        $markdown.Add("  - $queue")
    }
} else {
    $markdown.Add("  - (none)")
}
$markdown.Add("")
$markdown.Add("## UI CLI Commands")
$markdown.Add("")
if ($cliCommands.Count -gt 0) {
    foreach ($cmd in $cliCommands) {
        $markdown.Add("- $cmd")
    }
} else {
    $markdown.Add("- (none found)")
}
$markdown.Add("")
$markdown.Add("## Performance Sections")
$markdown.Add("")
if ($perfSections.Count -gt 0) {
    foreach ($sec in $perfSections) {
        $markdown.Add("- $sec")
    }
} else {
    $markdown.Add("- (none found)")
}
$markdown.Add("")
$markdown.Add("## Manual Context")
$markdown.Add("")
if ($manualExists) {
    $markdown.Add('- See `references/project_manual.md` for stable notes that should not be auto-generated.')
} else {
    $markdown.Add('- `references/project_manual.md` is missing.')
}

$snapshotMdPath = Join-Path $outDir "project_snapshot.md"
$snapshotJsonPath = Join-Path $outDir "project_snapshot.json"

Set-Content -LiteralPath $snapshotMdPath -Value ($markdown -join "`r`n") -Encoding UTF8

$snapshotObj = [ordered]@{
    generated_at = $generatedAt
    repo_root = $repo
    git = [ordered]@{
        branch = $branch
        head = $headSha
        latest_commit_subject = $headSubject
        latest_commit_date = $headDate
        dirty_files = $statusLines.Count
        recent_commits = $recentCommits
    }
    modules = $modules
    runtime_knobs = $knobs
    rtos = [ordered]@{
        tasks = $taskNames
        queues = $queueNames
    }
    ui_cli_commands = $cliCommands
    perf_sections = $perfSections
}

$snapshotObj | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $snapshotJsonPath -Encoding UTF8

Write-Host "Refreshed:"
Write-Host " - $snapshotMdPath"
Write-Host " - $snapshotJsonPath"
