#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
CONTEXT_H="$ROOT/arch/ppc/qemu/context.h"
CONTEXT_C="$ROOT/arch/ppc/qemu/context.c"
SWITCH_S="$ROOT/arch/ppc/qemu/switch.S"

for required in clang grep ln mktemp; do
    if ! command -v "$required" >/dev/null 2>&1; then
        printf 'error: required PPC context ABI test tool is missing: %s\n' \
            "$required" >&2
        exit 1
    fi
done

# The assembly context image is anchored at struct context + STKOFF. On
# PPC32, switch.S stores r31 at STKOFF + 36 * 4 = byte 152 and reserves
# 156 bytes total. The C structure therefore needs a distinct word at byte
# 152 for r31, with optional parameters starting at byte 156.
grep -Fq '#define STKOFF 8' "$SWITCH_S"
grep -Fq '#define SAVE_SPACE 156' "$SWITCH_S"
grep -Eq 'PPC_STL[[:space:]]+r31,[[:space:]]+\(STKOFF \+ 36 \* ULONG_SIZE\)\(r1\)' "$SWITCH_S"

# PPC64 uses a 48-byte linkage area. The context image must therefore place
# the saved r1 at byte 48, the entry PC at byte 56, and the final r31 slot at
# byte 336. switch.S adds 16 bytes to SAVE_SPACE for CONFIG_PPC64, so the
# PPC64 SAVE_SPACE value must be 328 to reserve the complete 344-byte image.
grep -Fq '#define STACKFRAME_MINSIZE 48' "$SWITCH_S"
grep -Fq '#define STKOFF STACKFRAME_MINSIZE' "$SWITCH_S"
grep -Fq '#define SAVE_SPACE 328' "$SWITCH_S"

# GCC -mcall-sysv-noeabi and LLVM's PPC SVR4 lowering both require a
# 16-byte-aligned C stack. init_context constructs r1 manually, so it must
# align the address of context.sp rather than depending on byte-array placement.
grep -Fq '#define PPC_STACK_ALIGNMENT 16' "$CONTEXT_C"
grep -Fq '__builtin_offsetof(struct context, sp)' "$CONTEXT_C"
grep -Fq 'PPC_STACK_ALIGNMENT - 1' "$CONTEXT_C"

scratch="$(mktemp -d "${TMPDIR:-/tmp}/openbios-ppc-context-abi.XXXXXX")"
trap 'rm -rf "$scratch"' EXIT

cat > "$scratch/layout.c" <<'SOURCE'
typedef unsigned char uint8_t;
typedef unsigned int uint32_t;
#include "arch/ppc/qemu/context.h"

#if defined(__powerpc64__)
_Static_assert(sizeof(unsigned long) == 8, "PPC64 unsigned long must be 8 bytes");
_Static_assert(__builtin_offsetof(struct context, return_addr) == 16,
               "PPC64 ABI LR save slot moved");
_Static_assert(__builtin_offsetof(struct context, sp) == 48,
               "PPC64 assembly STKOFF must address context.sp");
_Static_assert(__builtin_offsetof(struct context, pc) == 56,
               "PPC64 assembly entry slot must address context.pc");
_Static_assert(__builtin_offsetof(struct context, regs) == 64,
               "PPC64 context register bank offset changed");
_Static_assert(__builtin_offsetof(struct context, regs) + 34 * sizeof(unsigned long) == 336,
               "PPC64 r31 assembly slot moved");
_Static_assert(sizeof(struct context) == 344,
               "PPC64 context must include the r31 slot used by switch.S");
_Static_assert(__builtin_offsetof(struct context, param) == 344,
               "PPC64 optional parameters must follow the saved register image");
#else
_Static_assert(sizeof(unsigned long) == 4, "PPC32 unsigned long must be 4 bytes");
_Static_assert(__builtin_offsetof(struct context, return_addr) == 4,
               "PPC32 ABI LR save slot moved");
_Static_assert(__builtin_offsetof(struct context, sp) == 8,
               "PPC32 assembly STKOFF must address context.sp");
_Static_assert(__builtin_offsetof(struct context, pc) == 12,
               "PPC32 assembly entry slot must address context.pc");
_Static_assert(__builtin_offsetof(struct context, regs) == 16,
               "PPC32 context register bank offset changed");
_Static_assert(__builtin_offsetof(struct context, regs) + 34 * sizeof(unsigned long) == 152,
               "PPC32 r31 assembly slot moved");
_Static_assert(sizeof(struct context) == 156,
               "PPC32 context must include the r31 slot used by switch.S");
_Static_assert(__builtin_offsetof(struct context, param) == 156,
               "PPC32 optional parameters must follow the saved register image");
#endif
SOURCE

clang --target=powerpc-none-elf -m32 -mcpu=604 -msoft-float \
    -ffreestanding -fno-pic -fno-pie -fsyntax-only \
    -I"$ROOT" "$scratch/layout.c"
clang --target=powerpc64-none-elf -m64 -mcpu=970 -msoft-float \
    -ffreestanding -fno-pic -fno-pie -fsyntax-only \
    -I"$ROOT" "$scratch/layout.c"

# Exercise the two generated OpenBIOS configurations through the real
# switch.S preprocessor surface. Undefined ABI geometry macros would survive
# preprocessing and are rejected here before the assembler/linker can fail
# with opaque expression errors.
for mode in ppc32 ppc64; do
    mkdir -p "$scratch/$mode/include"
    ln -s "$ROOT/include/arch/ppc" "$scratch/$mode/include/asm"
done
cat > "$scratch/ppc32/include/autoconf.h" <<'EOF32'
#define CONFIG_PPC_64BITSUPPORT 1
EOF32
cat > "$scratch/ppc64/include/autoconf.h" <<'EOF64'
#define CONFIG_PPC64 1
EOF64

clang --target=powerpc-none-elf -m32 -mcpu=604 -msoft-float \
    -E -P -x assembler-with-cpp -I"$scratch/ppc32/include" \
    "$SWITCH_S" > "$scratch/switch-ppc32.i"
clang --target=powerpc64-none-elf -m64 -mcpu=970 -msoft-float \
    -E -P -x assembler-with-cpp -I"$scratch/ppc64/include" \
    "$SWITCH_S" > "$scratch/switch-ppc64.i"

for preprocessed in "$scratch/switch-ppc32.i" "$scratch/switch-ppc64.i"; do
    if grep -Eq '(^|[^[:alnum:]_])(STKOFF|ULONG_SIZE|SAVE_SPACE)([^[:alnum:]_]|$)' \
        "$preprocessed"; then
        printf 'error: unresolved PPC context ABI macro survived preprocessing: %s\n' \
            "$preprocessed" >&2
        exit 1
    fi
done

printf 'OpenBIOS PPC32/PPC64 context ABI layout: verified\n'
