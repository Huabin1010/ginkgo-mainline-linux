# 启动失败时如何捕获错误

## 我能自动捕获什么？

| 情况 | 能否捕获 | 方法 |
|------|----------|------|
| 内核 panic 后 **仍进 fastboot** | 部分 | `fastboot getvar`；**ramoops** 恢复 Android 后读 pstore |
| **USB adb 可用**（内核起了、init 未起） | ✅ | `adb shell dmesg` |
| **完全黑屏 / 反复重启** | ❌ 远程无法自动抓 | 需要 **UART 串口线** 或恢复 Android 后读 pstore |
| 恢复 LineageOS 后 | ✅ | `./scripts/capture-logs.sh` 读 pstore/last_kmsg |

**结论：** 没有串口线时，最可靠的是 **ramoops + 恢复 Android 后抓 pstore**。主线 ginkgo DTB 已启用 ramoops（`pstore_mem @ 0xffc00000`）。

---

## 推荐流程（刷机失败后）

```bash
# 1. 长按 音量下+电源 进 fastboot（若可以）

# 2. 恢复 Android boot（保留 pstore 内存内容）
./scripts/restore-android.sh

# 3. 进系统后立即抓日志
./scripts/capture-logs.sh

# 4. 把 backup/ginkgo/logs/ 里的文件发给我分析
```

---

## Serial 调试（最佳，可选）

ginkgo UART：`ttyMSM0, 115200n8`，地址 `0x4a90000`

- 需 **USB-TTL 串口线** 焊接到主板测试点（Redmi Note 8 社区有 pinout）
- 主机端：
  ```bash
  sudo apt install picocom
  picocom -b 115200 /dev/ttyUSB0
  ```
- boot.img cmdline 已含 `earlycon=msm_serial_dm,0x4a90000`

有串口时，从第一行 `Uncompressing Linux...` 到 panic 信息都能完整看到。

---

## 已内置的调试配置

- **earlycon** + **console=ttyMSM0**（boot.img cmdline）
- **CONFIG_PSTORE_RAM** + **ramoops**（DTS）
- **CONFIG_SERIAL_MSM_CONSOLE**

---

## 增强调试（可选，刷机前）

重建 boot.img 时使用更详细 cmdline：

```bash
CMDLINE="console=ttyMSM0,115200n8 earlycon=msm_serial_dm,0x4a90000 ignore_loglevel loglevel=8 \
  root=/dev/disk/by-partlabel/userdata rootwait rw init=/sbin/init" \
  ./scripts/build-bootimg.sh
```
