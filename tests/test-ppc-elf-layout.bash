#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
LDSCRIPT="$ROOT/arch/ppc/qemu/ldscript"

# The QEMU/OpenBIOS PPC PROM ABI is intentionally two loadable regions:
# one contiguous normal firmware image and one detached hard-reset vector.
# Keep this explicit so GNU ld and LLD cannot silently choose different
# PT_LOAD layouts for the same linker script.
grep -Fxq 'ENTRY(_start)' "$LDSCRIPT"
grep -Fxq 'PHDRS' "$LDSCRIPT"
grep -Eq '^[[:space:]]*firmware[[:space:]]+PT_LOAD[[:space:]]+FLAGS\(7\);$' "$LDSCRIPT"
grep -Eq '^[[:space:]]*reset[[:space:]]+PT_LOAD[[:space:]]+FLAGS\(5\);$' "$LDSCRIPT"

for section in '.text.vectors' '.text' '.rodata' '.data' '.bss'; do
    if ! awk -v section="$section" '
        index($0, section) { in_section=1 }
        in_section && /}[[:space:]]*:firmware[[:space:]]*$/ { found=1; exit }
        in_section && /^[[:space:]]*\.[A-Za-z0-9_.]+/ && !index($0, section) { exit }
        END { exit found ? 0 : 1 }
    ' "$LDSCRIPT"; then
        printf 'error: %s is not assigned to the firmware PT_LOAD\n' "$section" >&2
        exit 1
    fi
done

if ! awk '
    /\.romentry[[:space:]]*:/ { in_section=1 }
    in_section && /}[[:space:]]*:reset[[:space:]]*$/ { found=1; exit }
    END { exit found ? 0 : 1 }
' "$LDSCRIPT"; then
    echo 'error: .romentry is not assigned to the reset PT_LOAD' >&2
    exit 1
fi

printf 'OpenBIOS PPC ELF PT_LOAD ABI: verified\n'
