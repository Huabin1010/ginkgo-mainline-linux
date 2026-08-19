**Language:** English | [简体中文](zh-CN/mainline-boot-failure-analysis.md)

# Mainline kernel boot-failure analysis (2026-08-05)

Device: ginkgo `<serial>`. Symptom: after flashing `out/boot.img`, the phone returns to fastboot after a few seconds; no adb.

## Conclusions (by likelihood)

| # | Cause | Evidence | Severity | Status |
|---|------|------|--------|------|
| 0 | **`CONFIG_SM_GCC_6125` not enabled** | arm64 defconfig does **not** enable SM6125 GCC; without global clocks, UART/MMC/buses cannot work | **Fatal** | Being fixed |
| 1 | **`CONFIG_ARM64_VA_BITS_52=y` is unsuitable for SM6125** | Kryo 260 has no FEAT_LVA | **High** | Changed to 39 |
| 2 | **`qcom,board-id` does not match this unit** | This unit / `DTBO` = `0x22`; mainline was `0x16` | **High** | Changed to `0x22` |
| 3 | **Stock DTBO stacked onto the mainline DTB** | Empty DTBO already flashed for testing | Medium | Handled |
| 4 | **simplefb not enabled** | DTS uses `simple-framebuffer`, but `FB_SIMPLE` / `SYSFB_SIMPLEFB` were not enabled | Medium | Being fixed |
| 5 | pstore address mismatch | Mainline `@0xffc40000` vs TWRP `@0x61600000` | Medium (troubleshooting) | Pending |
| 6 | No serial port | No UART cable | Medium | — |

## Measured comparison

### board-id / msm-id

| Source | msm-id | board-id |
|------|--------|----------|
| Running DTS on this unit | `0x18a / 0x10000` | **`0x22 / 0x00`** |
| Mainline `sm6125-xiaomi-ginkgo.dtb` | `0x18a / 0x10000` | **`0x16 / 0x00` (wrong)** |
| Stock DTBO | `0x18a / 0x10000` | **`0x22 / 0x00` (match)** |

ABL selects the DTB by msm-id + board-id; a mismatch often returns straight to fastboot.

### boot.img format (already fixed)

The second packaging run already switched to header v2 + a standalone DTB, matching stock. It still failed → this is not the packaging format alone.

### Kernel-config suspects

```
CONFIG_ARM64_VA_BITS_52=y   # current out/kernel/.config — should be 39
```

Suggested fragment:

```
# CONFIG_ARM64_VA_BITS_52 is not set
CONFIG_ARM64_VA_BITS_39=y
CONFIG_ARM64_VA_BITS=39
```

## Fix steps (next)

1. Fix `config/ginkgo.fragment`: force `VA_BITS=39`
2. Fix the DTB: `qcom,board-id = <0x22 0>` (or provide both 0x16 and 0x22)
3. Unify the ramoops address (align with downstream `0x61600000` so recovery can capture logs)
4. Re-run `build-kernel.sh` + `build-bootimg.sh`
5. Flash boot from TWRP with `dd` (avoids fastboot USB stalls), or try `fastboot boot`
6. On failure, enter recovery and run `./scripts/capture-logs.sh` (after the pstore address is fixed)

## Why recovery cannot capture mainline logs

Mainline DTS:

```
ramoops@ffc00000 → reg = <0xffc40000 0xc0000>
```

TWRP kernel:

```
ramoops: attached 0x400000@0x61600000
```

Different addresses → empty `/sys/fs/pstore` is expected.

## Retest log (2026-08-05 01:00–01:16)

| Attempt | Result |
|------|------|
| VA_BITS=39 + board-id=0x22 | Still returns to fastboot |
| + empty DTBO + Ubuntu rootfs | Still returns to fastboot; also `fastboot reboot recovery` fails |
| + `CONFIG_SM_GCC_6125` + simplefb | Still returns to fastboot |
| Stock DTBO + GCC6125 boot | Still returns to fastboot |
| Restore stock boot+dtbo | Recovery works |

**Conclusion:** Known config-layer issues are fixed, but we still cannot get past ABL / the very early kernel. The next step must be **UART capture of earlycon** (see `docs/uart-debug-ginkgo.md`).
