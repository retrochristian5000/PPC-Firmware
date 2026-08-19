#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_xml="$ROOT/libgcc/build.xml"
header="$ROOT/libgcc/libgcc.h"
mod_source="$ROOT/libgcc/__moddi3.c"

for required in clang grep nm mktemp; do
    if ! command -v "$required" >/dev/null 2>&1; then
        printf 'error: required Clang runtime-helper test tool is missing: %s\n' \
            "$required" >&2
        exit 1
    fi
done

scratch="$(mktemp -d "${TMPDIR:-/tmp}/openbios-clang-runtime.XXXXXX")"
trap 'rm -rf "$scratch"' EXIT

cat > "$scratch/probe.c" <<'SOURCE'
long long signed_remainder(long long value, long long divisor)
{
    return value % divisor;
}
SOURCE

clang --target=powerpc-none-elf -m32 -mcpu=604 -msoft-float \
    -ffreestanding -fno-pic -fno-pie -O0 \
    -c "$scratch/probe.c" -o "$scratch/probe.o"

if ! nm -u "$scratch/probe.o" | grep -Eq '(^|[[:space:]])__moddi3$'; then
    printf '%s\n' \
        'error: Clang no longer lowers signed 64-bit PowerPC remainder to __moddi3.' \
        'Re-audit the OpenBIOS runtime-helper contract before changing this test.' >&2
    exit 1
fi

if [[ ! -f "$mod_source" ]]; then
    printf 'error: OpenBIOS is missing Clang-required helper: %s\n' \
        "$mod_source" >&2
    exit 1
fi
if ! grep -Fq '<object source="__moddi3.c"/>' "$build_xml"; then
    printf 'error: libgcc/build.xml does not include __moddi3.c\n' >&2
    exit 1
fi
if ! grep -Fq 'int64_t __moddi3(int64_t num, int64_t den);' "$header"; then
    printf 'error: libgcc/libgcc.h does not declare __moddi3\n' >&2
    exit 1
fi
if grep -Eq '[[:alnum:]_][[:space:]]*%[[:space:]]*[[:alnum:]_(]' "$mod_source"; then
    printf '%s\n' \
        'error: __moddi3 must not implement itself with signed remainder;' \
        'that would recurse through Clang runtime lowering.' >&2
    exit 1
fi

echo 'OpenBIOS Clang runtime-helper contract: verified'
