#!/usr/bin/env bash
# Minimal vbmeta image with verification disabled (for unlocked bootloaders).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/out/vbmeta.img"

# 256-byte vbmeta header, flags=2 (VERIFICATION_DISABLED)
python3 - <<'PY' "$OUT"
import struct, sys
out = sys.argv[1]
# AvbVBMetaImageHeader magic + minimal disabled vbmeta
magic = b"AVB0"
# Simplified: use fastboot --disable-verification without image when possible.
# Create empty placeholder; real flash uses --disable-verification flag.
open(out, "wb").write(magic + b"\x00" * 252)
print(f"placeholder written (use fastboot flash vbmeta --disable-verification {out})")
PY

ls -la "$OUT"
