#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
MAKEFILE="$ROOT/Makefile.target"

for required in grep make mktemp; do
    if ! command -v "$required" >/dev/null 2>&1; then
        printf 'error: required host-warning test tool is missing: %s\n' \
            "$required" >&2
        exit 1
    fi
done

# HOSTCC is allowed to be Clang in this fork, so GCC-only warnings must not be
# passed unconditionally to host-tool builds. The target warning remains in
# CFLAGS because the PowerPC Clang compatibility driver filters it there.
if grep -Eq '^HOSTCFLAGS\+=.*-Wbuiltin-declaration-mismatch' "$MAKEFILE"; then
    echo 'error: GCC-only builtin declaration warning is unconditional in HOSTCFLAGS' >&2
    exit 1
fi

grep -Fq 'host-cc-option = $(shell' "$MAKEFILE"
grep -Fq 'HOSTCFLAGS+= $(call host-cc-option,-Wbuiltin-declaration-mismatch)' "$MAKEFILE"
grep -Eq '^override CFLAGS \+= .*Wbuiltin-declaration-mismatch' "$MAKEFILE"

scratch="$(mktemp -d "${TMPDIR:-/tmp}/openbios-host-warning.XXXXXX")"
trap 'rm -rf "$scratch"' EXIT

grep '^host-cc-option = ' "$MAKEFILE" > "$scratch/probe.mk"
cat >> "$scratch/probe.mk" <<'EOF'
all:
	@printf '%s\n' '$(call host-cc-option,-Wbuiltin-declaration-mismatch)'
EOF

cat > "$scratch/accept-cc" <<'EOF'
#!/bin/sh
exit 0
EOF
cat > "$scratch/reject-cc" <<'EOF'
#!/bin/sh
exit 1
EOF
chmod +x "$scratch/accept-cc" "$scratch/reject-cc"

accepted="$(make --no-print-directory -f "$scratch/probe.mk" \
    HOSTCC="$scratch/accept-cc")"
rejected="$(make --no-print-directory -f "$scratch/probe.mk" \
    HOSTCC="$scratch/reject-cc")"

[[ "$accepted" == '-Wbuiltin-declaration-mismatch' ]]
[[ -z "$rejected" ]]

printf 'OpenBIOS host warning option probing: verified\n'
