#!/usr/bin/env python3
"""Dump every DSI controller register named in the mainline dsi.xml.

The offset table is generated on the build host from
drivers/gpu/drm/msm/registers/display/dsi.xml so it always matches what the
driver actually programs.  Note that dsi_host.c does
"msm_host->ctrl_base += cfg->io_offset", which is 4 for every DSI 6G
revision, so a register documented at XML offset N lives at hardware offset
N + 4.  6G_HW_VERSION is the one exception: it is read through the unshifted
base.
"""

import mmap
import os
import re
import struct
import sys

DSI0 = 0x05E94000
IO_OFFSET = 4
XML = "linux/drivers/gpu/drm/msm/registers/display/dsi.xml"


def parse_offsets(path):
    regs = []
    pat = re.compile(r'<reg32\s+offset="(0x[0-9a-fA-F]+)"\s+name="([A-Za-z0-9_]+)"')
    with open(path) as f:
        for line in f:
            m = pat.search(line)
            if m:
                name = m.group(2)
                off = int(m.group(1), 16)
                regs.append((off if name == "6G_HW_VERSION" else off + IO_OFFSET,
                             name))
    regs.sort()
    return regs


def emit_remote(regs):
    table = ",".join("(%#x,'%s')" % (o, n) for o, n in regs)
    return """
import mmap, os, struct
fd = os.open("/dev/mem", os.O_RDONLY | os.O_SYNC)
d = mmap.mmap(fd, 0x1000, mmap.MAP_SHARED, mmap.PROT_READ, offset=%#x)
os.close(fd)
for off, name in [%s]:
    print("  %%-32s @%%#05x = %%#010x" %% (name, off,
          struct.unpack_from("<I", d, off)[0]))
""" % (DSI0, table)


def main():
    regs = parse_offsets(XML)
    if not regs:
        print("failed to parse %s" % XML, file=sys.stderr)
        return 1
    sys.stdout.write(emit_remote(regs))
    return 0


if __name__ == "__main__":
    sys.exit(main())
