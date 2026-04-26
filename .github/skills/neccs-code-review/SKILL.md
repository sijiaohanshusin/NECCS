---
name: neccs-code-review
description: "Perform a systematic code review for NECCS project files. Use when reviewing changed C files, checking ARM Compiler 5 compatibility, validating naming conventions, real-time safety, or running the full quality gate checklist."
argument-hint: "List the files to review, or say 'all changed files'"
---
# NECCS Code Review

## When to Use

- After implementing any code change (T5 in Tech Lead workflow)
- When asked to review specific files
- Before committing or delivering code
- When checking if code meets project quality gates

## Review Procedure

### Step 1: Identify Changed Files

List all modified `.c` and `.h` files. For each file, note:
- What was changed (added/modified/deleted)
- Which subsystem it belongs to (Algorithm / App / BSP / Hardware / common)

### Step 2: Run Checklist

For each changed file, check every item in the [full checklist](./references/checklist.md). Mark each as PASS / FAIL / N/A.

### Step 3: Classify Findings

Severity levels:
- **BLOCKER**: Will cause crash, data corruption, or build failure. Must fix before delivery.
- **HIGH**: Violates a Key Constraint or will cause subtle bugs. Must fix.
- **MEDIUM**: Style violation, missing docs, or maintainability concern. Should fix, may defer with justification.
- **LOW**: Cosmetic or preference. Note but don't block.

### Step 4: Report

Output findings in severity order with exact file and line references:

```
[REVIEWER] BLOCKER — file.c:42 — DMA buffer not in D2 SRAM
[REVIEWER] HIGH    — file.c:87 — malloc() used in Audio task
[REVIEWER] MEDIUM  — file.h:12 — Header guard uses single underscore
```
