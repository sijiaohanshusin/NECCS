---
description: "Use when writing or reviewing C code for ARM Compiler 5 (armcc). Covers C99 patterns, banned C11/GNU extensions, section attributes, pragma syntax, and common compatibility pitfalls."
---
# ARM Compiler 5 (armcc) Compatibility

This project uses ARM Compiler 5 (`armcc`), NOT `armclang` or `gcc`. Many C11 and GNU extensions are unsupported.

## Allowed (C99)

- `//` single-line comments
- Variable declarations at any point in a block (C99)
- `for (int i = 0; ...)` — loop variable declaration
- `__attribute__((section("...")))` — memory placement
- `__attribute__((aligned(N)))` — alignment
- `__packed` keyword (ARM extension)
- `#pragma pack(push, 1)` / `#pragma pack(pop)`
- Designated initializers: `{ .field = value }`
- Compound literals: `(type){ ... }` (limited support)

## Banned (will NOT compile)

- `_Generic` (C11)
- `_Static_assert` / `static_assert` (C11) — use `#if` / `#error` instead
- `_Alignas` / `_Alignof` (C11) — use `__attribute__((aligned(N)))` or `__ALIGNOF()`
- `__auto_type` (GNU)
- `typeof()` (GNU) — use explicit type
- `__attribute__((cleanup(...)))` (GNU)
- `__attribute__((constructor))` / `((destructor))` (GNU)
- Statement expressions `({ ... })` (GNU)
- Variable-length arrays (VLA) — use fixed-size arrays or `#define` constants
- `_Thread_local` / `__thread` (C11) — use FreeRTOS task-local storage
- Flexible array members at end of struct: `type arr[];` — use `type arr[1];` instead

## Pragma Differences

```c
// armcc pragma syntax
#pragma arm section rwdata = "section_name"   // place following data in section
#pragma arm section                           // reset to default

// armcc equivalent of GCC __attribute__
__attribute__((section(".RAM_D2"))) uint8_t dma_buf[SIZE];

// armcc-specific optimization
#pragma Otime    // optimize for speed
#pragma Ospace   // optimize for size
```

## Common Pitfalls

1. **Anonymous structs/unions**: Supported but emit warning — suppress with `#pragma anon_unions`
2. **Inline functions**: Use `__inline` or `static inline` — `extern inline` has different semantics than GCC
3. **Bit-field signedness**: Unqualified bit-fields are unsigned in armcc (signed in GCC) — always specify `signed` or `unsigned`
4. **Enum size**: armcc uses smallest type that fits; GCC defaults to `int` — use `--enum_is_int` if needed
5. **String concatenation across macros**: Works but be careful with macro hygiene
6. **Function pointer casts**: armcc is stricter — avoid casting between incompatible function signatures
