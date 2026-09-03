#!/usr/bin/env python3
"""Regression checks for the PowerMac3,1 Open Firmware identity contract."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
INIT = (ROOT / "arch/ppc/qemu/init.c").read_text()
MACIO = (ROOT / "drivers/macio.c").read_text()
DRIVERS = (ROOT / "include/drivers/drivers.h").read_text()


def require(text: str, needle: str, label: str) -> None:
    if needle not in text:
        raise SystemExit(f"missing {label}: {needle}")


# ABI: QEMU publishes a NUL-terminated ASCII OF model in this named fw_cfg file.
require(INIT, '#define FW_CFG_PPC_BOARD_ID_FILE "etc/ppc/board-id"',
        "PowerPC board-id fw_cfg filename")
require(INIT, '#define POWERMAC3_1_BOARD_ID "PowerMac3,1"',
        "PowerMac3,1 board id")
require(INIT, "machine_id != ARCH_MAC99", "mac99 architecture guard")
require(INIT, "board_id_size == sizeof(POWERMAC3_1_BOARD_ID)",
        "NUL-inclusive board-id size check")
require(INIT, "memcmp(board_id, POWERMAC3_1_BOARD_ID,",
        "exact board-id comparison")
require(DRIVERS, "extern int is_powermac3_1(void);",
        "shared PowerMac3,1 predicate")

# The Apple TN2001 PowerMac3,1 dump lists exactly these root compatible strings.
start = INIT.index("/* PowerMac3,1 root identity: Apple TN2001 */")
end = INIT.index("/* Generic machine identity follows. */", start)
sawtooth_root = INIT[start:end]
for compatible in ('"PowerMac3,1"', '"MacRISC"', '"Power Macintosh"'):
    require(sawtooth_root, compatible, f"Sawtooth root compatible {compatible}")
if '"MacRISC2"' in sawtooth_root:
    raise SystemExit("PowerMac3,1 root must not advertise MacRISC2")
require(sawtooth_root, 'push_str("bootrom")', "Sawtooth bootrom device type")
require(sawtooth_root, 'push_str("clock-frequency")',
        "Sawtooth root clock-frequency")

# Sawtooth-only details must not leak back into generic mac99.
usb_start = MACIO.index("static void ob_unin_set_sawtooth_usb_clock_ids")
usb_end = MACIO.index("static int macio_nvram_shift", usb_start)
usb_block = MACIO[usb_start:usb_end]
require(usb_block, "if (!is_powermac3_1())", "Sawtooth USB identity gate")

alias_start = MACIO.index("static void ob_unin_set_sawtooth_aliases")
alias_end = MACIO.index("static int macio_nvram_shift", alias_start)
alias_block = MACIO[alias_start:alias_end]
require(alias_block, "find_dev(aliases[i].path)", "alias target existence guard")
require(alias_block, "if (!is_powermac3_1())", "Sawtooth alias gate")
for alias, path in (
    ("pci0", "/pci@f0000000"),
    ("agp", "/pci@f0000000"),
    ("pci1", "/pci@f2000000"),
    ("pci2", "/pci@f4000000"),
    ("bridge", "/pci@f2000000/pci-bridge@d"),
    ("pci", "/pci@f2000000/pci-bridge@d"),
    ("usb0", "/pci@f2000000/pci-bridge@d/usb@8"),
    ("usb1", "/pci@f2000000/pci-bridge@d/usb@9"),
    ("mac-io", "/pci@f2000000/pci-bridge@d/mac-io@7"),
    ("mpic", "/pci@f2000000/pci-bridge@d/mac-io@7/interrupt-controller"),
    ("scca", "/pci@f2000000/pci-bridge@d/mac-io@7/escc/ch-a"),
    ("sccb", "/pci@f2000000/pci-bridge@d/mac-io@7/escc/ch-b"),
    ("via-pmu", "/pci@f2000000/pci-bridge@d/mac-io@7/via-pmu"),
):
    require(alias_block, f'{{ "{alias}", "{path}" }}', f"Sawtooth alias {alias}")

print("PowerMac3,1 Open Firmware contract: ok")
