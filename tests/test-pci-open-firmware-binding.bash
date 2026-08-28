#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
pci_c="$repo_root/drivers/pci.c"
vga_fs="$repo_root/drivers/vga.fs"

fail()
{
    echo "error: $*" >&2
    exit 1
}

assigned_body=$(awk '
    /static void pci_set_assigned_addresses/ { in_func = 1 }
    in_func { print }
    in_func && /^}/ { exit }
' "$pci_c")

if ! grep -Eq 'flags[[:space:]]*\|[[:space:]]*IS_NOT_RELOCATABLE|IS_NOT_RELOCATABLE[[:space:]]*\|[[:space:]]*flags' <<<"$assigned_body"; then
    fail "assigned-addresses must mark assigned BARs non-relocatable"
fi

if ! grep -Eq '\{[[:space:]]*"map-in",[[:space:]]*ob_pci_bus_map_in' "$pci_c"; then
    fail "PCI bus node must expose the IEEE 1275 map-in method"
fi

if ! grep -Eq '\{[[:space:]]*"map-in",[[:space:]]*ob_pci_bridge_map_in' "$pci_c"; then
    fail "PCI bridge node must expose the IEEE 1275 map-in method"
fi

if ! grep -Fq 'call_parent_method("map-in");' "$pci_c"; then
    fail "PCI bridges must chain the standard map-in method"
fi

# Keep the old private spelling temporarily so existing FCode remains compatible.
if ! grep -Eq '\{[[:space:]]*"pci-map-in",[[:space:]]*ob_pci_bus_map_in' "$pci_c"; then
    fail "PCI bus must retain pci-map-in as a compatibility alias"
fi

map_in_calls=$(grep -Fc '" map-in" $call-parent' "$vga_fs" || true)
if (( map_in_calls < 2 )); then
    fail "QEMU VGA FCode must use the standard parent map-in method for BAR0 and BAR2"
fi

if grep -Fq '" pci-map-in" $call-parent' "$vga_fs"; then
    fail "QEMU VGA FCode must not depend on the private pci-map-in spelling"
fi

echo "Open Firmware PCI video binding: verified"
