#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
LDSCRIPT="$ROOT/arch/ppc/qemu/ldscript"

# Keep the PowerPC PROM load ABI explicit without collapsing writable data
# into executable code.  LLD naturally separates these permission domains;
# encode that W^X-safe layout in the linker script so it cannot regress.
grep -Fxq 'ENTRY(_start)' "$LDSCRIPT"
grep -Fxq 'PHDRS' "$LDSCRIPT"
grep -Eq '^[[:space:]]*text[[:space:]]+PT_LOAD[[:space:]]+FLAGS\(5\);$' "$LDSCRIPT"
grep -Eq '^[[:space:]]*rodata[[:space:]]+PT_LOAD[[:space:]]+FLAGS\(4\);$' "$LDSCRIPT"
grep -Eq '^[[:space:]]*data[[:space:]]+PT_LOAD[[:space:]]+FLAGS\(6\);$' "$LDSCRIPT"
grep -Eq '^[[:space:]]*reset[[:space:]]+PT_LOAD[[:space:]]+FLAGS\(5\);$' "$LDSCRIPT"

if grep -Eq 'PT_LOAD[[:space:]]+FLAGS\(7\)' "$LDSCRIPT"; then
    echo 'error: PPC OpenBIOS linker script contains an RWE PT_LOAD' >&2
    exit 1
fi

check_section_segment()
{
    local section=$1
    local segment=$2

    if ! awk -v section="$section" -v segment="$segment" '
        index($0, section) { in_section=1 }
        in_section && $0 ~ "}[[:space:]]*:" segment "[[:space:]]*$" { found=1; exit }
        in_section && /^[[:space:]]*\.[A-Za-z0-9_.]+/ && !index($0, section) { exit }
        END { exit found ? 0 : 1 }
    ' "$LDSCRIPT"; then
        printf 'error: %s is not assigned to the %s PT_LOAD\n' \
            "$section" "$segment" >&2
        exit 1
    fi
}

check_section_segment '.text.vectors' text
check_section_segment '.text' text
check_section_segment '.rodata' rodata
check_section_segment '.data' data
check_section_segment '.bss' data
check_section_segment '.romentry' reset

printf 'OpenBIOS PPC W^X ELF PT_LOAD ABI: verified\n'
