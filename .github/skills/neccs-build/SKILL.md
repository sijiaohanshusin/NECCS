---
name: neccs-build
description: "Build the NECCS firmware using Keil MDK and analyze build results. Use when you need to compile the STM32H743 project, check for build errors/warnings, or validate code changes against the Keil toolchain."
argument-hint: "Optional: describe what you changed before building"
---
# NECCS Firmware Build

## When to Use

- After modifying any `.c` or `.h` file under `START/`
- When asked to fix build errors or warnings
- When validating that code changes compile correctly
- As the final gate (T6) in the Tech Lead workflow

## Build Procedure

### Step 1: Run Keil Build

Execute the build from the MDK-ARM directory:

```powershell
cd START/MDK-ARM
C:\Keil_v5\UV4\UV4.exe -b START.uvprojx -j0 -t START -o build_log.txt
```

Exit codes: `0` = success, `1` = warnings only, `2` = errors present.

### Step 2: Read Build Log

Read [build_log.txt](../../START/MDK-ARM/build_log.txt) and look for:

1. **Errors** (`error` keyword): Must be zero. Fix all errors before proceeding.
2. **Warnings** (`warning` keyword): Categorize as new vs. pre-existing. New warnings need justification.
3. **Final summary line**: e.g., `"START\START.axf" - 0 Error(s), 2 Warning(s)`

### Step 3: Report Results

Quote the final summary line verbatim. For any errors:
1. Read the error message and source file/line
2. Open the referenced source file
3. Identify the root cause
4. Apply the fix
5. Re-run the build

## Common Build Errors

See [references/errors.md](./references/errors.md) for a catalog of frequently encountered errors and their fixes.

## Build Configuration

- **Target**: `START`
- **Compiler**: ARM Compiler V5.06 update 7 (`armcc`)
- **Build time**: ~9s full, ~6s incremental
- **Output**: `START/MDK-ARM/START/START.axf`
