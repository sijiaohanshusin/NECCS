---
description: "Fix Keil build errors. Runs the build, reads build_log.txt, locates errors, and applies fixes automatically."
agent: "agent"
tools: [read, search, edit, execute]
argument-hint: "Optional: describe the error or paste the error message"
---
Fix build errors in the NECCS STM32H743 project.

## Steps

1. Run the Keil build:
   ```powershell
   cd START/MDK-ARM
   C:\Keil_v5\UV4\UV4.exe -b START.uvprojx -j0 -t START -o build_log.txt
   ```
2. Read `START/MDK-ARM/build_log.txt`
3. For each error found:
   - Identify the source file and line number
   - Read the source file to understand context
   - Apply the fix
4. Re-run the build to verify
5. Report the final build summary line verbatim

If the user provided a specific error message, start from step 3 using that error.

Use the `neccs-build` skill for common error patterns and fixes.
