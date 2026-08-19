**语言：** [English](README.md) | 简体中文

# ginkgo 刷机前备份

备份时间：2026-08-05  
设备序列号：<serial>  
ROM：LineageOS 17.1-20220214-NIGHTLY-ginkgo (Android 10)

## 文件

| 文件 | 大小 | 用途 |
|------|------|------|
| `boot.img` | 64 MB | **恢复 LineageOS 内核**（最重要） |
| `vbmeta.img` | 64 KB | Verified Boot 元数据 |
| `dtbo.img` | 24 MB | Device Tree Overlay |
| `device-info.txt` | — | ROM 版本信息 |
| `cmdline.txt` | — | 内核 cmdline 参考 |
| `partitions.txt` | — | 分区列表 |

## 恢复 Android

```bash
adb reboot bootloader
./scripts/restore-android.sh
```

或手动：

```bash
fastboot flash boot backup/ginkgo/boot.img
fastboot flash vbmeta backup/ginkgo/vbmeta.img
fastboot flash dtbo backup/ginkgo/dtbo.img
fastboot reboot
```

若仍无法进入系统（userdata 已被 Ubuntu 覆盖），在 TWRP 中 wipe data，或 `fastboot -w`（会清空数据）。

## 重新备份

```bash
./scripts/backup-device.sh
```
