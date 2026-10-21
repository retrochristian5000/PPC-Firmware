#!/usr/bin/env python3
"""Compile and execute the real OpenBIOS fw_cfg bootorder formatter on the host."""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "arch/ppc/qemu/init.c").read_text(encoding="utf-8")

start = SOURCE.index("static char *\nppc_format_bootorder(")
end = SOURCE.index("\nstruct cpudef {", start)
helper = SOURCE[start:end]
assert "strncat" not in helper
assert "strlen(suffix[0])" in helper
assert "data[i] != '\\n'" in helper
assert "data[i] != '\\0'" in helper
assert 'ppc_has_guest_startup_disk(boot_device)' in SOURCE
assert 'free(boot_device);' in SOURCE
assert 'strcmp(device, "disk") != 0' in helper

harness = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
''' + helper + r'''

static int verify(const char *name, const char *data, uint32_t length,
                  const char *expected)
{
    char *value = ppc_format_bootorder(data, length);
    int ok = expected ? value && strcmp(value, expected) == 0 : !value;

    if (!ok) {
        fprintf(stderr, "%s: expected %s, got %s\n", name,
                expected ? expected : "(null)", value ? value : "(null)");
    }
    free(value);
    return ok ? 0 : 1;
}

int main(void)
{
    static const char no_nul[] = { 'h', 'd' };
    static const char with_nul[] = { 'h', 'd', '\0', 'x' };
    static const char long_path[301] = { [0 ... 299] = 'x', [300] = '\n' };
    char expected_long[1000];
    char large[65537];
    int failures = 0;

    failures += verify("non-NUL fw_cfg", no_nul, sizeof(no_nul),
                       "hd:,\\\\:tbxi hd:,\\ppc\\bootinfo.txt hd:,%BOOT ");
    failures += verify("two fallback devices", "hd\ncd\n", 6,
                       "hd:,\\\\:tbxi hd:,\\ppc\\bootinfo.txt hd:,%BOOT "
                       "cd:,\\\\:tbxi cd:,\\ppc\\bootinfo.txt cd:,%BOOT ");
    failures += verify("CRLF", "hd\r\n", 4,
                       "hd:,\\\\:tbxi hd:,\\ppc\\bootinfo.txt hd:,%BOOT ");
    failures += verify("first NUL terminates", with_nul, sizeof(with_nul),
                       "hd:,\\\\:tbxi hd:,\\ppc\\bootinfo.txt hd:,%BOOT ");
    failures += verify("empty input", "", 0, NULL);
    failures += verify("empty lines", "\n\n", 2, NULL);
    failures += verify("NULL input", NULL, 42, NULL);

    /* The absence of a saved preference permits QEMU fallbacks. */
    if (ppc_has_guest_startup_disk(NULL) ||
        ppc_has_guest_startup_disk("") ||
        ppc_has_guest_startup_disk("disk") ||
        !ppc_has_guest_startup_disk("hd") ||
        !ppc_has_guest_startup_disk("mac-io/ata-4@1f000/@0:8") ||
        !ppc_has_guest_startup_disk("enet")) {
        fprintf(stderr, "Startup Disk precedence regression failed\n");
        failures++;
    }

    memset(expected_long, 0, sizeof(expected_long));
    snprintf(expected_long, sizeof(expected_long),
             "%.*s:,\\\\:tbxi %.*s:,\\ppc\\bootinfo.txt %.*s:,%s ",
             300, long_path, 300, long_path, 300, long_path, "%BOOT");
    failures += verify("long path cannot be truncated", long_path,
                       sizeof(long_path), expected_long);

    memset(large, 'x', sizeof(large));
    failures += verify("oversize fw_cfg entry", large, sizeof(large), NULL);

    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
'''

with tempfile.TemporaryDirectory(prefix="openbios-bootorder-") as dirname:
    source = Path(dirname) / "bootorder.c"
    binary = Path(dirname) / "bootorder"
    source.write_text(harness, encoding="utf-8")
    cc = shlex.split(os.environ.get("CC", "cc"))
    subprocess.run(cc + ["-std=gnu99", "-Wall", "-Wextra", "-Werror",
                         str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)

print("OpenBIOS PPC fw_cfg bootorder formatting: passed")
