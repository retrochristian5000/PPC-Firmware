#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
pci_c="$repo_root/drivers/pci.c"
pci_fs="$repo_root/drivers/pci.fs"

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

# PCI addresses are bus-domain addresses.  Preserve each host bridge's
# translation context so map-in can translate secondary UniNorth roots.
if ! grep -Fq 'OB_PCI_IO_BASE_PROP' "$pci_c" ||
   ! grep -Fq 'OB_PCI_MEM_BASE_PROP' "$pci_c" ||
   ! grep -Fq 'pci_bus_addr_to_host_addr_for_node' "$pci_c"; then
    fail "PCI host nodes must retain per-root address translation context"
fi

echo "Open Firmware PCI binding foundations: verified"
