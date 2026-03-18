---
name: neccs-project-context
description: Maintain and use an up-to-date AI-readable context package for the NECCS repository. Use when starting a new conversation on this project, answering environment or architecture questions, preparing code changes, reviewing performance regressions, or handing work off between sessions.
---

# NECCS Project Context

## Quick Start

1. Run `scripts/refresh_context.ps1 -RepoRoot <repo-path>`.
2. Read `references/project_snapshot.md` for the latest generated context.
3. Read `references/project_manual.md` for stable human-maintained notes.

## Standard Workflow

1. Refresh context at the beginning of project work.
2. Refresh again after large refactors, merge/rebase, or configuration changes.
3. Use generated snapshot first, then open source files only where extra depth is needed.

## Edit Safety

When preparing code changes in this repo:

1. Never guess a target path. Use `rg --files` or `Test-Path` to verify the real file path before editing.
2. Before the first edit, read the exact function or region you plan to change from the verified file.
3. If multiple files share the same name, stop and choose the correct path before patching.
4. Prefer full repo-relative paths with all intermediate directories, for example `START/User/App/app_display.c`.
5. If `apply_patch` is rejected, do not retry the same patch blindly. Re-check the path, re-read the current file content, and rebuild a smaller patch from the latest context.
6. If a PowerShell script or another tool has already modified the file, treat any older patch context as stale and regenerate it from the current file.

## Freshness Rules

- Treat `references/project_snapshot.md` as stale when git `HEAD` changes.
- Treat it as stale when uncommitted changes touch boot, RTOS task, display, or performance files.
- If stale, rerun `scripts/refresh_context.ps1` before analysis.

## Files

- `references/project_snapshot.md`: Auto-generated concise project context for reading.
- `references/project_snapshot.json`: Auto-generated structured snapshot for tooling.
- `references/project_manual.md`: Human-maintained baseline info (board, toolchain, process).

## Optional Automatic Update

Run `scripts/install_git_hook.ps1 -RepoRoot <repo-path>` once.  
It installs `post-commit` and `post-merge` hooks that refresh snapshot automatically.
