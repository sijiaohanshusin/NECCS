---
description: "Run a systematic code review on recent changes using the NECCS project quality gates and checklist."
agent: "neccs-code-reviewer"
tools: [read, search]
argument-hint: "List the files to review, or say 'all recent changes'"
---
Perform a full NECCS code review on the specified files or recent changes.

Use the `neccs-code-review` skill checklist. For each file:

1. Check naming conventions (Module_FunctionName, header guards with double underscore)
2. Check ARM Compiler 5 compatibility (no C11/GNU extensions)
3. Check real-time safety (no malloc/printf in audio/ISR, LVGL only in UI_Task)
4. Check memory placement (DMA buffers in D2 SRAM, FFT scratch in DTCM)
5. Check error handling (Err_t returns, HAL status checked)
6. Check documentation (Doxygen on public functions)

Output findings in severity order:
```
[REVIEWER] BLOCKER — file:line — description
[REVIEWER] HIGH    — file:line — description
[REVIEWER] MEDIUM  — file:line — description
```

If no files are specified, check what files have been recently modified in the working tree using `git diff --name-only`.
