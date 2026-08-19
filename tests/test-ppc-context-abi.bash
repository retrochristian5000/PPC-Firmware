#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
CONTEXT_H="$ROOT/arch/ppc/qemu/context.h"
CONTEXT_C="$ROOT/arch/ppc/qemu/context.c"
SWITCH_S="$ROOT/arch/ppc/qemu/switch.S"

for required in clang grep mktemp; do
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
grep -Fq 'PPC_STL  r31, (STKOFF + 36 * ULONG_SIZE)(r1)' "$SWITCH_S"
grep -Fq '#define SAVE_SPACE 156' "$CONTEXT_C"

# GCC -mcall-sysv-noeabi and LLVM's PPC32 SVR4 lowering both require a
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

_Static_assert(sizeof(unsigned long) == 4, "PPC32 unsigned long must be 4 bytes");
_Static_assert(__builtin_offsetof(struct context, sp) == 8,
               "assembly STKOFF must address context.sp");
_Static_assert(__builtin_offsetof(struct context, pc) == 12,
               "assembly entry slot must address context.pc");
_Static_assert(__builtin_offsetof(struct context, regs) == 16,
               "context register bank offset changed");
_Static_assert(__builtin_offsetof(struct context, regs) + 34 * sizeof(unsigned long) == 152,
               "r31 assembly slot moved");
_Static_assert(sizeof(struct context) == 156,
               "context must include the r31 slot used by switch.S");
_Static_assert(__builtin_offsetof(struct context, param) == 156,
               "optional parameters must not overlap the r31 save slot");
SOURCE

clang --target=powerpc-none-elf -m32 -mcpu=604 -msoft-float \
    -ffreestanding -fno-pic -fno-pie -fsyntax-only \
    -I"$ROOT" "$scratch/layout.c"

printf 'OpenBIOS PPC context ABI layout: verified\n'
