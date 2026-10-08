#!/usr/bin/env python3
"""PReP identity and OF client-interface ABI regression guards.

Static source assertions protect the contract. They do not assert that
OpenBIOS implements a Windows NT ARC runtime or that NT has booted.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
INIT = (ROOT / "arch/ppc/qemu/init.c").read_text(encoding="utf-8")
CLIENT = (ROOT / "libopenbios/client.c").read_text(encoding="utf-8")


def require(src: str, fragment: str) -> None:
    if fragment not in src:
        raise SystemExit(f"missing PReP/OF interface contract: {fragment}")


# Keep the actual machine ID dispatch distinct from the Apple root identity.
switch = INIT.split("switch(machine_id) {", 1)[1]
prep = switch.split("case ARCH_PREP:", 1)[1].split("case ARCH_MAC99:", 1)[0]
apple = switch.split("case ARCH_MAC99:", 1)[1].split("}", 1)[0]

require(prep, 'push_str("PReP");')
require(prep, 'push_str("prep");')
require(prep, 'push_str("compatible");')
require(prep, 'push_str("clock-frequency");')
if "PowerMac3,1" in prep or "MacRISC" in prep or "AAPL," in prep:
    raise SystemExit("PReP root contains Apple machine identity")
require(apple, 'push_str("PowerMac3,1");')
require(apple, 'push_str("MacRISC");')

# CIF call-method/interpret must always publish catch in return cell 0.
special = CLIENT.split("handle_calls(prom_args_t *pb)", 1)[1]
special = special.split("of_client_interface(int *params)", 1)[0]
require(special, "pb->args[pb->nargs] = val;")
catch_write = special.index("pb->args[pb->nargs] = val;")
error = special.index("if (val) {", catch_write)
assert catch_write < error, "catch must be written before a caught failure"
regular = CLIENT.split("of_client_interface(int *params)", 1)[1]
require(regular, "if (pb->nret < 1)")
require(regular, "return handle_calls(pb);")

print("PReP machine identity and OF CIF catch contract: verified")
