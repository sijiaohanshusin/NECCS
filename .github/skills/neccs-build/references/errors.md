# Common NECCS Build Errors

## Linker Errors

### L6218E: Undefined symbol
**Cause**: Function declared in header but not defined, or `.c` file not added to Keil project.
**Fix**: Check that the `.c` file is included in the Keil project tree. Open `START.uvprojx` and verify.

### L6406W: No space in execution region
**Cause**: Binary exceeds Flash or RAM region.
**Fix**: Check linker map (`START.map`) for largest consumers. Consider optimizing const data or moving buffers.

## Compiler Errors

### Error #20: Identifier not found
**Cause**: Missing `#include` or typo in function/variable name.
**Fix**: Verify include path and spelling. Check include order: HAL → CMSIS → FreeRTOS → BSP → App → Algorithm.

### Error #18: Expected a ")"
**Cause**: Often a C11/GNU syntax incompatible with ARM Compiler 5.
**Fix**: Check for VLA, `_Static_assert`, `typeof`, or statement expressions. See `arm-compiler-5.instructions.md`.

### Error #119: Cast not allowed
**Cause**: armcc is stricter about function pointer casts than GCC.
**Fix**: Use compatible function signatures or add an intermediate `void*` cast with comment explaining safety.

## Compiler Warnings

### Warning #1-D: Last line not terminated with newline
**Fix**: Add an empty line at end of file.

### Warning #223-D: Function declared implicitly
**Cause**: No prototype visible at callsite.
**Fix**: Add `#include` for the header containing the function declaration.

### Warning #550-D: Variable set but never used
**Fix**: Remove unused variable or add `(void)var;` if intentionally unused.

## Section Placement Errors

### L6314W: No section matches pattern
**Cause**: Scatter file references a section name (`.dtcm`, `.RAM_D2`) but no variable uses it.
**Fix**: Verify `__attribute__((section("...")))` spelling matches scatter file exactly.
