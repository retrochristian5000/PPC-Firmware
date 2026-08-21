#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/libc/vsprintf.c"

python3 - "$SRC" <<'PY'
import re
import sys
from pathlib import Path

src = Path(sys.argv[1]).read_text()

checks = [
    (r"case 'X':(?P<body>.*?)case 'x':", "%X -> %x"),
    (r"case 'i':(?P<body>.*?)case 'u':", "%d/%i -> %u"),
]

for pattern, label in checks:
    match = re.search(pattern, src, re.S)
    if not match:
        raise SystemExit(f"could not locate {label} switch path")
    body = match.group("body")
    if "__attribute__((fallthrough));" not in body:
        raise SystemExit(f"{label} lacks a compiler-recognized fallthrough annotation")

print("vsprintf fallthrough annotations: verified")
PY
