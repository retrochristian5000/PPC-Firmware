#!/usr/bin/env python3
"""Static contract for the OpenBIOS PowerPC primary framebuffer.

Runtime display-device-tree and multi-monitor tests remain necessary.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
pci = (root / "drivers/pci.c").read_text()
vga = (root / "drivers/vga.fs").read_text()
display = (root / "forth/device/display.fs").read_text()


def require(haystack, needle):
    if needle not in haystack:
        raise SystemExit(f"missing firmware multi-display contract: {needle}")


callback = pci.split("int vga_config_cb (const pci_config_t *config)", 1)[1]
callback = callback.split("int ebus_config_cb(", 1)[0]
require(callback, "if (!video.w) {\n                setup_video();")
require(callback, """feval("['] vga-driver-fcode 2 cells + 1 byte-load");""")
require(callback, '"driver,AAPL,MacOS,PowerPC"')

install = vga.split(": qemu-vga-driver-install ( -- )", 1)[1]
install = install.split(": qemu-vga-driver-init", 1)[0]
require(install, "qemu-edid-install")
require(install, 'fb-addr encode-int " address" property')
require(install, "display-ih 0= if")
assert install.index('fb-addr encode-int " address" property') < install.index(
    "display-ih 0= if"
)
guard = install.split("display-ih 0= if", 1)[1].split("      then", 1)[0]
for phrase in ("fb-addr to frame-buffer-adr", "fb8-install",
               '0 0 " AAPL,boot-display" property'):
    require(guard, phrase)
require(display, 'display-ih encode-int " display" property')
print("OpenBIOS primary boot-display isolation contract: verified")
