# ginkgo pre-flash backup

**Language:** English | [简体中文](README.zh-CN.md)

Backup date: 2026-08-05  
ROM: LineageOS 17.1-20220214-NIGHTLY-ginkgo (Android 10)

Image files (`*.img`) are gitignored. Keep a local copy if you still need Android.

## Files

| File | Size | Use |
|------|------|-----|
| `boot.img` | 64 MB | **Restore the LineageOS kernel** (most important) |
| `vbmeta.img` | 64 KB | Verified Boot metadata |
| `dtbo.img` | 24 MB | Device Tree Overlay |
| `device-info.txt` | — | ROM version |
| `cmdline.txt` | — | Kernel cmdline reference |
| `partitions.txt` | — | Partition list |

## Restore Android

```bash
adb reboot bootloader
./scripts/restore-android.sh
```

Or by hand:

```bash
fastboot flash boot backup/ginkgo/boot.img
fastboot flash vbmeta backup/ginkgo/vbmeta.img
fastboot flash dtbo backup/ginkgo/dtbo.img
fastboot reboot
```

If the phone still will not boot (userdata already overwritten with Ubuntu), wipe data in TWRP or run `fastboot -w` (this erases data).

## Re-backup

```bash
./scripts/backup-device.sh
```
