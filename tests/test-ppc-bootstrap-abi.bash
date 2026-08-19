#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
SWITCH_ARCH="$ROOT/config/scripts/switch-arch"
PPC_TYPES="$ROOT/include/arch/ppc/types.h"
CROSS_H="$ROOT/kernel/cross.h"

for required in awk cc grep mktemp; do
    if ! command -v "$required" >/dev/null 2>&1; then
        printf 'error: required PPC bootstrap ABI test tool is missing: %s\n' \
            "$required" >&2
        exit 1
    fi
done

# PowerPC OpenBIOS intentionally keeps 32-bit Forth cells even for the
# dedicated PPC64 C target. forthstrap must compare this cell width against
# the host pointer width; using the PPC64 C long width here truncates host
# pointers on 64-bit bootstrap hosts.
grep -Eq '^typedef[[:space:]]+uint32_t[[:space:]]+ucell;' "$PPC_TYPES"
grep -Eq '^typedef[[:space:]]+int32_t[[:space:]]+cell;' "$PPC_TYPES"
grep -Eq '^#define[[:space:]]+BITS[[:space:]]+32$' "$PPC_TYPES"

scratch="$(mktemp -d "${TMPDIR:-/tmp}/openbios-ppc-bootstrap-abi.XXXXXX")"
trap 'rm -rf "$scratch"' EXIT

# Extract only the pure architecture-classification helpers. Do not execute
# switch-arch's build-tree mutation logic during this regression test.
awk '
/^(is_bigendian|longbits|cellbits|crosscflags)\(\)$/ { emit=1 }
emit { print }
emit && /^}$/ { emit=0 }
' "$SWITCH_ARCH" > "$scratch/functions.sh"
# shellcheck disable=SC1090
source "$scratch/functions.sh"

expect_cross_flags()
{
    local host=$1
    local target=$2
    local endian_flag=$3
    local width_flag=$4

    CROSSCFLAGS=
    crosscflags "$host" "$target"
    case " $CROSSCFLAGS " in
        *" $endian_flag "*) ;;
        *)
            printf 'error: %s -> %s missing endian flag %s: %s\n' \
                "$host" "$target" "$endian_flag" "$CROSSCFLAGS" >&2
            exit 1
            ;;
    esac
    case " $CROSSCFLAGS " in
        *" $width_flag "*) ;;
        *)
            printf 'error: %s -> %s missing cell-width flag %s: %s\n' \
                "$host" "$target" "$width_flag" "$CROSSCFLAGS" >&2
            exit 1
            ;;
    esac
}

# Apple Silicon and ordinary x86_64 hosts are little-endian and 64-bit.
# Both PPC32 and PPC64 OpenBIOS dictionaries remain big-endian with 32-bit
# cells, so they need byte swapping and the narrower-native-cell pointer path.
expect_cross_flags aarch64 ppc \
    -DSWAP_ENDIANNESS -DNATIVE_BITWIDTH_SMALLER_THAN_HOST_BITWIDTH
expect_cross_flags aarch64 ppc64 \
    -DSWAP_ENDIANNESS -DNATIVE_BITWIDTH_SMALLER_THAN_HOST_BITWIDTH
expect_cross_flags amd64 ppc64 \
    -DSWAP_ENDIANNESS -DNATIVE_BITWIDTH_SMALLER_THAN_HOST_BITWIDTH

# A big-endian 64-bit PPC host still has wider pointers than OpenBIOS cells,
# but it must not byte-swap the dictionary.
expect_cross_flags ppc64 ppc64 \
    -USWAP_ENDIANNESS -DNATIVE_BITWIDTH_SMALLER_THAN_HOST_BITWIDTH

# A 32-bit host pointer fits the 32-bit PPC64 OpenBIOS cell exactly.
expect_cross_flags x86 ppc64 \
    -DSWAP_ENDIANNESS -DNATIVE_BITWIDTH_EQUALS_HOST_BITWIDTH

# Exercise the real cross.h swap and pointer-translation macros used by
# forthstrap on a 64-bit little-endian host. The PPC dictionary cell stays
# 32-bit in both PPC32 and PPC64 builds, while host pointers are represented as
# offsets from base_address instead of being truncated to a ucell.
cat > "$scratch/endian.c" <<'SOURCE'
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef uint32_t ucell;
typedef int32_t cell;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
#define BITS 32
#define SWAP_ENDIANNESS 1
#define CONFIG_BIG_ENDIAN 1
#define NATIVE_BITWIDTH_SMALLER_THAN_HOST_BITWIDTH 1
#include "kernel/cross.h"

unsigned long base_address;

int main(void)
{
    ucell cell_value = 0x11223344U;
    ucell storage = 0;
    unsigned char *bytes = (unsigned char *)&storage;
    unsigned char region[64];
    void *pointer = &region[37];
    ucell encoded;

    if (sizeof(void *) <= sizeof(ucell))
        return 10;

    base_address = (unsigned long)(uintptr_t)region;
    encoded = pointer2cell(pointer);
    if (encoded != 37U)
        return 11;
    if (cell2pointer(encoded) != pointer)
        return 12;

    if (target_ucell(cell_value) != 0x44332211U)
        return 1;
    write_ucell(&storage, cell_value);
    if (bytes[0] != 0x11 || bytes[1] != 0x22 ||
        bytes[2] != 0x33 || bytes[3] != 0x44)
        return 2;
    if (read_ucell(&storage) != cell_value)
        return 3;
    return 0;
}
SOURCE

cc -std=c11 -Wall -Wextra -Werror -I"$ROOT" \
    "$scratch/endian.c" -o "$scratch/endian"
"$scratch/endian"

printf 'OpenBIOS PPC bootstrap width/endian ABI: verified\n'
