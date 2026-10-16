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

# HOSTCC may be Clang, so GCC-only warnings must be probed before they are
# passed to host tools. PowerPC target CFLAGS remain protected separately by
# the Clang compatibility driver.
if grep -Eq '^HOSTCFLAGS\\+=[[:space:]]+-Wbuiltin-declaration-mismatch([[:space:]]|$)' "$MAKEFILE"; then
    echo 'error: GCC-only builtin declaration warning is unconditional in HOSTCFLAGS' >&2
    exit 1
fi

grep -Fq 'host-cc-option = $(shell' "$MAKEFILE"
if grep -Eq '^HOSTCFLAGS\\+=[[:space:]]+-Wno-maybe-uninitialized([[:space:]]|$)' "$MAKEFILE"; then
    echo 'error: GCC-only maybe-uninitialized warning is unconditional in HOSTCFLAGS' >&2
    exit 1
fi

grep -Fq 'HOSTCFLAGS+= $(call host-cc-option,-Wno-maybe-uninitialized)' "$MAKEFILE"
grep -Fq 'HOSTCFLAGS+= $(call host-cc-option,-Wbuiltin-declaration-mismatch)' "$MAKEFILE"
grep -Eq '^override CFLAGS \+= .*Wbuiltin-declaration-mismatch' "$MAKEFILE"

scratch="$(mktemp -d "${TMPDIR:-/tmp}/openbios-host-warning.XXXXXX")"
trap 'rm -rf "$scratch"' EXIT

grep '^host-cc-option = ' "$MAKEFILE" > "$scratch/probe.mk"
cat >> "$scratch/probe.mk" <<'EOF'
all:
	@printf '%s\n' '$(call host-cc-option,-Wbuiltin-declaration-mismatch)'
	@printf '%s\n' '$(call host-cc-option,-Wno-maybe-uninitialized)'
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

expected="$(printf '%s\n' '-Wbuiltin-declaration-mismatch' '-Wno-maybe-uninitialized')"
[[ "$accepted" == "$expected" ]]
[[ -z "$rejected" ]]

# Compile with the real Clang host warning selection when available. This
# catches a regression where one GCC-only flag leaks through while -Werror
# is active, without assuming future Clang versions reject the same options.
if command -v clang >/dev/null 2>&1; then
    cat > "$scratch/config.mak" <<'EOF'
ARCH=ppc
SRCDIR=.
EOF
    : > "$scratch/rules.mak"
    cp "$MAKEFILE" "$scratch/Makefile"
    cat >> "$scratch/Makefile" <<'EOF'

.PHONY: print-host-flags
print-host-flags:
	@printf '%s\n' '$(HOSTCFLAGS)'
EOF
    host_flags="$(make --no-print-directory -s -C "$scratch" \
        HOSTCC=clang print-host-flags)"
    read -r -a host_flag_array <<< "$host_flags"
    printf 'int openbios_host_warning_probe;\n' |
        clang -Werror "${host_flag_array[@]}" -x c -fsyntax-only -
fi

printf 'OpenBIOS host warning option probing: verified\n'
