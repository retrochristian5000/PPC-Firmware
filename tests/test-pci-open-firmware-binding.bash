#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
pci_c="$repo_root/drivers/pci.c"
pci_fs="$repo_root/drivers/pci.fs"
vga_fs="$repo_root/drivers/vga.fs"

fail()
{
    echo "error: $*" >&2
    exit 1
}

# IEEE 1275 PCI assigned-addresses entries are not ordered like reg entries.
# The consumer must select an entry by its BAR/register field in phys.hi.
if ! grep -Fq 'dup ff and 6 pick = if' "$pci_fs"; then
    fail "pci-bar>pci-addr must match assigned-addresses by BAR register"
fi

# PCI addresses are bus-domain addresses. Preserve each host bridge's
# translation context so map-in can translate secondary UniNorth roots.
if ! grep -Fq 'OB_PCI_IO_BASE_PROP' "$pci_c" ||
   ! grep -Fq 'OB_PCI_MEM_BASE_PROP' "$pci_c" ||
   ! grep -Fq 'pci_bus_addr_to_host_addr_for_node' "$pci_c"; then
    fail "PCI host nodes must retain per-root address translation context"
fi

assigned_body=$(awk '
    /static void pci_set_assigned_addresses/ { in_func = 1 }
    in_func { print }
    in_func && /^}/ { exit }
' "$pci_c")

if ! grep -Eq 'flags[[:space:]]*\|[[:space:]]*IS_NOT_RELOCATABLE|IS_NOT_RELOCATABLE[[:space:]]*\|[[:space:]]*flags' <<<"$assigned_body"; then
    fail "assigned-addresses must mark assigned BARs non-relocatable"
fi

map_body=$(awk '
    /ob_pci_bus_map_in\(int \*idx\)/ { in_func = 1 }
    in_func { print }
    in_func && /^}/ { exit }
' "$pci_c")

for cell_name in phys_lo phys_mid phys_hi; do
    if ! grep -Fq "$cell_name = POP();" <<<"$map_body"; then
        fail "map-in must consume $cell_name from the IEEE 1275 PCI address tuple"
    fi
done

if ! grep -Fq 'pci_resolve_map_address' <<<"$map_body"; then
    fail "map-in must resolve relocatable versus assigned PCI addresses"
fi

if ! grep -Eq '\{[[:space:]]*"map-in",[[:space:]]*ob_pci_bus_map_in' "$pci_c"; then
    fail "PCI bus node must expose the standard map-in method"
fi

if ! grep -Eq '\{[[:space:]]*"pci-map-in",[[:space:]]*ob_pci_bus_map_in' "$pci_c"; then
    fail "PCI bus node must retain pci-map-in as a compatibility alias"
fi

if ! grep -Eq '\{[[:space:]]*"map-in",[[:space:]]*ob_pci_bridge_map_in' "$pci_c"; then
    fail "PCI bridge node must expose the standard map-in method"
fi

if ! grep -Fq 'call_parent_method("map-in");' "$pci_c"; then
    fail "PCI bridges must chain the standard map-in method"
fi

if (( $(grep -Fc '" map-in" $call-parent' "$vga_fs" || true) < 2 )); then
    fail "QEMU VGA FCode must map BAR0 and BAR2 through standard map-in"
fi

if grep -Fq '" pci-map-in" $call-parent' "$vga_fs"; then
    fail "QEMU VGA FCode must not depend on the private pci-map-in spelling"
fi

echo "Open Firmware PCI mapping binding: verified"
