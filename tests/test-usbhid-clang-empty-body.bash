#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
usbhid="$ROOT/drivers/usbhid.c"

modifier_block="$(awk '
    /int i, keypress = 0, modifiers = 0;/ { in_block=1 }
    in_block { print }
    in_block && /Did the event change at all/ { exit }
' "$usbhid")"

# GUI modifiers are intentionally ignored by OpenBIOS. They must not be
# represented as empty if statements: Clang diagnoses those with -Wempty-body,
# and the conditions have no runtime effect anyway.
if grep -Eq 'if \(current->modifiers & 0x(08|80)\)[^;]*;' <<< "$modifier_block"; then
    printf '%s\n' \
        'error: USB HID modifier handling contains an empty GUI if statement.' >&2
    exit 1
fi

grep -Fq 'GUI modifiers are intentionally ignored.' <<< "$modifier_block"

grep -Fq 'if (current->modifiers & 0x01) /* Left-Ctrl */' <<< "$modifier_block"
grep -Fq 'if (current->modifiers & 0x02) /* Left-Shift */' <<< "$modifier_block"
grep -Fq 'if (current->modifiers & 0x04) /* Left-Alt */' <<< "$modifier_block"
grep -Fq 'if (current->modifiers & 0x10) /* Right-Ctrl */' <<< "$modifier_block"
grep -Fq 'if (current->modifiers & 0x20) /* Right-Shift */' <<< "$modifier_block"
grep -Fq 'if (current->modifiers & 0x40) /* Right-AltGr */' <<< "$modifier_block"

printf 'USB HID Clang empty-body policy: verified\n'
