#!/bin/sh
# Configuration-only regression test; no cross compiler or ROM is required.
set -eu
src=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir "$work/bin" "$work/build"
# Use real executable files rather than relying on a shell builtin.
for tool in gcc ar ld nm strip ranlib toke xsltproc; do
    case "$tool" in
        toke|xsltproc) name="$tool" ;;
        *) name="test-ppc-$tool" ;;
    esac
    printf '#!/bin/sh\nexit 0\n' > "$work/bin/$name"
    chmod 700 "$work/bin/$name"
done
cat > "$work/bin/uname" <<'EOF'
#!/bin/sh
case "$1" in
    -m) printf '%s\n' arm64e ;;
    -s) printf '%s\n' Darwin ;;
    *) exit 1 ;;
esac
EOF
chmod 700 "$work/bin/uname"
PATH="$work/bin:$PATH"
export PATH
TOKE="$work/bin/toke"
CROSS_COMPILE="$work/bin/test-ppc-"
HOSTARCH=
ODIRS=stale
export TOKE CROSS_COMPILE HOSTARCH ODIRS
cd "$work/build"
"$src/config/scripts/switch-arch" qemu-ppc64 qemu-ppc
grep -Eq '^override TARGETS :=[[:space:]]+ppc64 ppc$' config-host.mak
grep -q '^override HOSTARCH := aarch64$' obj-ppc/config.mak
grep -q '^override CROSSCFLAGS := -DSWAP_ENDIANNESS -DNATIVE_BITWIDTH_SMALLER_THAN_HOST_BITWIDTH$' obj-ppc/config.mak
grep -q '^override OPENBIOS_CONFIG_CFLAGS := -m32 ' obj-ppc/config.mak
"$src/config/scripts/switch-arch" qemu-ppc
test "$(grep -c '^override TARGETS :=' config-host.mak)" -eq 1
grep -Eq '^override TARGETS :=[[:space:]]+ppc$' config-host.mak
if grep -Eq 'stale|obj-ppc64' config-host.mak; then
    printf '%s\n' 'error: stale build directories survived configuration' >&2
    exit 1
fi
make_cmd=${MAKE_CMD:-make}
info=$("$make_cmd" -s -C obj-ppc info CFLAGS='-arch arm64e -O3' TARGET=wrong- ARCH=wrong)
printf '%s\n' "$info" | grep -F "Target compiler: ${CROSS_COMPILE}gcc"
printf '%s\n' "$info" | grep -F 'Target CFLAGS: -m32 '
if printf '%s\n' "$info" | grep -Eq -- '-arch arm64e|-O3'; then
    printf '%s\n' 'error: parent flags reached firmware compilation' >&2
    exit 1
fi
rm "$work/bin/test-ppc-ld"
if "$src/config/scripts/switch-arch" qemu-ppc; then
    printf '%s\n' 'error: configuration accepted a missing cross linker' >&2
    exit 1
fi
printf '%s\n' 'OpenBIOS configuration variable regression checks passed'
