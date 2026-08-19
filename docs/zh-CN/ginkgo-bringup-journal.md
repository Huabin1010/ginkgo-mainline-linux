**语言：** [English](../ginkgo-bringup-journal.md) | 简体中文

# Redmi Note 8 (ginkgo) 主线 Linux 适配工作日志

> 设备序列号：`<serial>`  
> 平台：Qualcomm SM6125 (Kryo 260)  
> 内核：Linux 7.0.0 主线 + `sm6125-xiaomi-ginkgo.dts`  
> 文档更新：2026-08-08（启动/init 阶段日志，**冻结**）  
> **显示 P3 已于 2026-08-17 打通：** [ginkgo-display-complete-2026-08-17.md](ginkgo-display-complete-2026-08-17.md)  
> **触控 P4 已于 2026-08-17 打通：** [ginkgo-touch-complete-2026-08-17.md](ginkgo-touch-complete-2026-08-17.md)  
> **Docker 已于 2026-08-19 打通：** [ginkgo-docker-2026-08-19.md](ginkgo-docker-2026-08-19.md)  
> 串口日志目录：`backup/ginkgo/logs/`

本文档汇总截至 2026-08-07 的适配工作：从「刷入即回 fastboot」到「内核完整启动并进入 Android init」的全过程，并结合已保存的 UART 日志逐条说明原因与修复。

---

## 1. 项目目标

在 **Redmi Note 8 (codename `ginkgo`)** 上运行 **主线 Linux 内核**，最终挂载 Ubuntu rootfs 并进入用户态（systemd），而非 LineageOS/Android。

调试手段以 **USB-TTL 串口** 为主（早期 boot / panic 信息），辅以 recovery adb 刷分区。

---

## 2. 设备与环境

| 项目 | 内容 |
|------|------|
| 型号 | Xiaomi Redmi Note 8 (ginkgo) |
| SoC | Qualcomm SM6125 |
| 存储 | eMMC 58.2 GiB (`mmc1`，地址 `4744000.sdhci`) |
| 原厂 ROM | LineageOS 17.1 (Android 10)，备份于 `backup/ginkgo/` |
| 显示 | NT36672A Tianma 1080×2340 DSI（**主线 DRM 已出图**，2026-08-17） |
| 调试 UART | TP0003=TX (GPIO16)，TP0012=RX (GPIO17)，**1.8V**，115200 8N1 |
| 主机串口设备 | `/dev/ttyACM1` |
| 内核构建 | `out/kernel/`，产物 `out/boot.img` / `out/boot-debug.img` |

### 2.1 接线与监听

详见 [`docs/ginkgo-usb-ttl-uart.md`](ginkgo-usb-ttl-uart.md)、[`docs/uart-debug-ginkgo.md`](uart-debug-ginkgo.md)。

```bash
# 持续监听并自动保存日志
sudo python3 scripts/uart-monitor.py
# 日志写入 backup/ginkgo/logs/uart-YYYYMMDD-HHMMSS.log
```

**原则：先开串口监听，再开机/重启。**

---

## 3. 时间线与里程碑

| 日期 | 阶段 | 现象 | 结果 |
|------|------|------|------|
| 2026-08-05 | 早期刷机（无 UART） | 刷 `boot.img` 后数秒回 fastboot，无 adb | 见 [`mainline-boot-failure-analysis.md`](mainline-boot-failure-analysis.md)；`boot-test-005614.txt` 记录 24 次均在 fastboot |
| 2026-08-07 22:40–23:08 | UART 接线调试 | 多个空日志（未开机或波特率/接线问题） | `uart-20260807-224051.log` 等为空 |
| 2026-08-07 23:30 | 首次有效 UART | ABL 报 DTBO overlay 失败 | `uart-20260807-233001.log` |
| 2026-08-07 23:40 | 空 DTBO + 主线 boot | 内核启动，~5.76s TLMM panic | `uart-20260807-233836.log` |
| 2026-08-07 23:47–23:50 | 修复 `soc@0` 地址格式后重刷 | **相同 TLMM panic**（根因不在地址格式） | `uart-20260807-234759.log` |
| 2026-08-07 23:52 | 修复 `gpio-reserved-ranges` 后重刷 | **内核完整启动**，~31s 进入 Android init 后重启 | `uart-20260807-235321.log`（约 1.2 MB，含多次重启循环） |

---

## 4. 已完成工作

### 4.1 构建与刷机基础设施

| 脚本 | 作用 |
|------|------|
| `scripts/build-kernel.sh` | 编译 `Image.gz` + `sm6125-xiaomi-ginkgo.dtb` |
| `scripts/build-bootimg.sh` | 打包 Android boot image v2（含独立 DTB） |
| `scripts/build-debug-boot.sh` | 合并 `ginkgo-debug.fragment`，生成 `boot-debug.img` |
| `scripts/make-empty-dtbo.sh` | 生成 24MB 全零 DTBO，绕过 ABL overlay |
| `scripts/flash-mainline-test.sh` | 一键：空 dtbo + boot-debug + reboot（recovery/fastboot） |
| `scripts/uart-monitor.py` | 串口监听与日志落盘 |
| `scripts/build-rootfs.sh` 等 | Ubuntu arm64 rootfs → `out/rootfs.ext4`（尚未刷入 userdata） |
| `scripts/restore-android.sh` | 从 `backup/ginkgo/` 恢复原厂 boot/dtbo/vbmeta |

### 4.2 内核配置（`config/ginkgo.fragment`）

关键项：

- `CONFIG_ARM64_VA_BITS=39` — Kryo 260 不支持 52-bit VA
- `CONFIG_SM_GCC_6125=y` — SM6125 全局时钟，**无此驱动则外设无法工作**
- `CONFIG_SERIAL_QCOM_GENI_CONSOLE` / `CONFIG_SERIAL_MSM_CONSOLE` — 串口控制台
- `CONFIG_PINCTRL_SM6125=y` — TLMM pinctrl
- `CONFIG_FB_SIMPLE` / `CONFIG_SYSFB_SIMPLEFB` — simple-framebuffer
- `CONFIG_MMC_SDHCI_MSM` / `CONFIG_EXT4_FS` — eMMC + rootfs

调试附加（`config/ginkgo-debug.fragment`）：`CONFIG_INITCALL_DEBUG`、`loglevel=8` 等。

### 4.3 设备树修改

**板级 `sm6125-xiaomi-ginkgo.dts`：**

- `qcom,board-id = <0x22 0>` — 与本机/原厂 DTBO 一致（曾为错误的 `0x16`）
- `aliases { serial0 = &uart4; }` + `chosen.stdout-path`
- `&uart4 { status = "okay"; }` — 调试串口
- `&qupv3_id_0 { status = "okay"; }`
- **`&tlmm { gpio-reserved-ranges = <0 4>, <30 4>; }`** — 见 §5.3

**SoC `sm6125.dtsi`：**

- `soc@0` 总线改为 2-cell 地址（与 sm6115 一致）：
  ```dts
  #address-cells = <2>;
  #size-cells = <2>;
  ranges = <0 0 0 0 0x10 0>;
  ```
- 其下设备 `reg` 统一为 `<0x0 addr 0x0 size>` 四元组
- 新增 `uart4`（`qcom,geni-debug-uart` @ `0x4a90000`）及 `qup_uart4_default` pinctrl

### 4.4 备份

`backup/ginkgo/` 含原厂 `boot.img`、`dtbo.img`、`vbmeta.img`、`cmdline.txt`、`partitions.txt`，可随时恢复 Android。

---

## 5. 启动问题排查全记录

### 5.1 阶段 A：刷入即回 fastboot（2026-08-05，无 UART）

**现象：** 连续 24 次启动均在 fastboot，adb 不可用。

```
# backup/ginkgo/logs/boot-test-005614.txt（节选）
[1] adb=none fb=<serial>  fastboot
...
[24] adb=none fb=<serial>  fastboot
```

**已识别并修复的配置问题：**

| 问题 | 修复 |
|------|------|
| `CONFIG_ARM64_VA_BITS_52` | 改为 39 |
| `qcom,board-id` 不匹配 | 改为 `0x22` |
| `CONFIG_SM_GCC_6125` 未开 | 加入 fragment |
| boot.img 打包格式 | header v2 + 独立 DTB |

修复后仍回 fastboot → 需要 UART 才能看到 ABL/内核内部错误（见阶段 B）。

---

### 5.2 阶段 B：ABL 拦截 — DTBO overlay 失败

**日志：** `backup/ginkgo/logs/uart-20260807-233001.log`（13 KB）

```
ApplyOverlay: ufdt apply overlay failed
Error: Dtb overlay failed
Launching fastboot
```

**原因：** 原厂 `dtbo.img` 中的 overlay 与主线 `sm6125-xiaomi-ginkgo.dtb` 结构不兼容，ABL 在加载内核前失败。

**修复：** 刷入 24MB 全零 `out/dtbo-empty.img`（`scripts/make-empty-dtbo.sh`），ABL 跳过 overlay，仅使用 boot.img 内嵌 DTB。

**标准测试命令：**

```bash
./scripts/flash-mainline-test.sh
```

---

### 5.3 阶段 C：TLMM pinctrl SError panic（~5.76s）

**日志：**

- `uart-20260807-233836.log`（53 KB，含完整 initcall 跟踪）
- `uart-20260807-234759.log`（4 KB，仅 panic 段）

**典型 panic 栈：**

```
[    5.759820][    C5] SError Interrupt on CPU5, code 0x00000000bf000002
[    5.759913][    C5] pc : gpiochip_add_data_with_key+0x6a0/0xee0
[    5.760232][    C5] Kernel panic - not syncing: Asynchronous SError Interrupt
...
[    5.760596][    C5]  sm6125_tlmm_probe+0x18/0x40
[    5.760904][    C5]  of_platform_populate+0x84/0x170
```

**分析过程：**

1. 曾怀疑 `soc@0` 的 `#address-cells` 错误导致 TLMM 物理地址映射错误；修复为 2-cell 后 **panic 依旧**。
2. 用 `fdtget` 验证 DTB 中 TLMM `reg` 已正确：
   ```
   0 5242880  0 4194304   → 0x500000, size 0x400000 (west)
   0 9437184  0 4194304   → 0x900000 (south)
   0 13631488 0 4194304   → 0xd00000 (east)
   ```
3. 反汇编确认 panic 发生在 `gpiochip_add` 内对 **GPIO0** 调用 `get_direction` 读硬件寄存器时触发 SError。
4. 对照上游补丁与小米下游：`gpio-reserved-ranges` 配置错误。

**根因：** `sm6125-xiaomi-ginkgo.dts` 中错误的保留 GPIO 范围：

```dts
/* 错误 — 导致内核去读 QUP0 专用引脚寄存器 */
gpio-reserved-ranges = <22 2>, <28 6>;
```

**修复（已合入 DTS）：**

```dts
/* QUP0 (gpio0-3) 与 QUP6 (gpio30-33)；在 ginkgo 上读取这些 ctl reg 会 fault */
gpio-reserved-ranges = <0 4>, <30 4>;
```

参考：[linux-kernel 邮件列表 — Fix reserved gpio ranges for ginkgo](https://lists.openwall.net/linux-kernel/2026/01/13/680)

---

### 5.4 阶段 D：内核成功启动，Android init 失败（~31s）

**日志：** `backup/ginkgo/logs/uart-20260807-235321.log`（约 1.2 MB，含 5 次完整内核启动、8 次 reboot）

**成功标志（首次在日志中出现）：**

```
[    4.954222][    T1] initcall sm6125_tlmm_init+0x0/0x28 returned 0 after 0 usecs
[   29.620907][   T56] probe of 1400000.clock-controller returned 0 after 96813 usecs
[   29.737701][   T93] mmc1: SDHCI controller on 4744000.mmc [4744000.mmc] using ADMA 64-bit
[   29.835387][   T62] mmcblk1: mmc1:0001 3H6CAB 58.2 GiB
[   29.834692][   T56] printk: legacy console [ttyMSM0] enabled
[   14.371169][    T1] simple-framebuffer 5c000000.framebuffer: fb0: simplefb registered!
[   31.089827][    T1] Freeing unused kernel memory: 3328K
[   31.105517][    T1] Run /init as init process
```

**随后 Android init 崩溃：**

```
[   31.839031][    T1] init: mount("selinuxfs", "/sys/fs/selinux", "selinuxfs", ...) failed No such file or directory
[   31.862718][   T69] mmc0: Card stuck being busy! __mmc_poll_for_busy
[   31.862738][    T1] init: Init encountered errors starting first stage, aborting
[   32.079547][    T1] init: ... InitFatalReboot()
[   32.780000][    T1] reboot: Restarting system with command 'bootloader'
```

**原因说明：**

| 因素 | 说明 |
|------|------|
| Bootloader 追加 cmdline | 末尾 `init=/init`、`root=PARTUUID=...`、`skip_initramfs` 覆盖我们的 `init=/sbin/init` |
| 空 ramdisk | `build-bootimg.sh` 生成空 `initramfs.cpio.gz`，内核无 ramdisk `/init` 可执行 |
| 无 Ubuntu rootfs | `out/rootfs.ext4` 已构建，但 **userdata 仍为 Android 分区**，未刷入 |
| Android init 依赖 | 需要 SELinux、SD 卡槽 (mmc0)、大量 Android 专有驱动 |

**结论：** 内核侧「能启动」已达成；用户态需 **initramfs + 刷 Ubuntu rootfs**，见 §7。

---

## 6. 日志文件索引

| 文件 | 大小 | 内容摘要 |
|------|------|----------|
| `boot-test-005614.txt` | 831 B | 2026-08-05：24 次刷机均在 fastboot |
| `uart-20260807-224051.log` | 0 | 空（监听时未开机） |
| `uart-20260807-225955.log` | 12 B | 几乎无数据 |
| `uart-20260807-231308.log` | 0 | 空 |
| `uart-20260807-232950.log` | 0 | 空 |
| `uart-20260807-233001.log` | 13 KB | **DTBO overlay 失败** → fastboot |
| `uart-20260807-233836.log` | 53 KB | **TLMM SError panic**（修复 gpio-reserved 前） |
| `uart-20260807-234759.log` | 4 KB | 同上 panic（仅捕获 panic 段） |
| `uart-20260807-235321.log` | **1.2 MB** | **修复后完整启动** + Android init 重启循环 |
| `uart-live-*.log` | 极小 | 早期测试片段 |

### 6.1 如何阅读 `uart-20260807-235321.log`

该文件最有价值，建议按关键字搜索：

```bash
# 内核版本与 cmdline
strings backup/ginkgo/logs/uart-20260807-235321.log | grep "Linux version"

# TLMM 是否通过
strings ... | grep sm6125_tlmm

# eMMC
strings ... | grep mmcblk1

# 用户态
strings ... | grep -E "Run /init|Init encountered|reboot:"

# 每次启动以 "Linux version" 或 Bootloader 输出分隔
strings ... | grep -c "Linux version"   # 本文件约 5 次
```

---

## 7. 内核 cmdline 合并行为

Bootloader 会将原厂参数 **追加** 到 boot.img 内 cmdline 之后。实测合并结果（来自 `235321` 日志）：

```
console=ttyMSM0,115200n8 earlycon=qcom_geni,0x4a90000 ...
root=/dev/disk/by-partlabel/userdata rootwait rw init=/sbin/init
...
root=PARTUUID=54dc1022-3967-9c82-4fd9-bf8e9137d187
skip_initramfs rootwait ro init=/init
```

**生效规则：**

- 后面的 `root=`、`init=` 覆盖前面的 → 最终跑 **Android `/init`**
- `init=/sbin/init`（我们的 Ubuntu）被忽略
- ramdisk 为空时，内核在 rootfs 上找不到可用的 early `/init`，直接 mount Android 分区

这也是下一阶段必须用 **非空 initramfs（含 `/init` 脚本）** 的原因。

---

## 8. 当前状态（2026-08-08）

### 8.1 已打通

- [x] USB-TTL 串口调试（115200，`/dev/ttyACM1`）
- [x] ABL 加载主线 DTB（空 DTBO）
- [x] 内核 earlycon + 完整驱动 probe
- [x] TLMM / GCC / SDHCI / UART4 / simplefb
- [x] eMMC 识别（`mmcblk1`，58.2 GiB）
- [x] 内核释放内存并启动用户态进程

### 8.2 未完成

- [ ] initramfs（劫持 `init=/init`）
- [ ] 刷入 `out/rootfs.ext4` 到 userdata
- [x] systemd 正常启动（后续已打通，见 chronicle）
- [x] DRM/DSI 真显示（2026-08-17 品红上屏）
- [x] SPI 触控 NT36672A（2026-08-17 点屏出点）
- [ ] WiFi 等

### 8.3 当前刷机产物

| 文件 | 说明 |
|------|------|
| `out/boot-debug.img` | 含 gpio-reserved 修复 + initcall_debug |
| `out/boot.img` | 正式 boot（需重新打包以包含最新 DTS） |
| `out/dtbo-empty.img` | 空 DTBO |
| `out/rootfs.ext4` | Ubuntu rootfs 镜像（约 2GB，**未刷入**） |

---

## 9. 下一步：让 Linux 正常进入系统

按优先级排列：

### 9.1 实现 initramfs（P0）

在 `boot.img` ramdisk 中提供 `/init` 脚本，在挂载 Android 根分区之前执行：

1. `mount` proc/sysfs/devtmpfs
2. 等待 `/dev/disk/by-partlabel/userdata`
3. `mount` ext4 到 `/newroot`
4. `switch_root /newroot /sbin/init`

需新增 `scripts/build-initramfs.sh`，并修改 `build-bootimg.sh`（不再生成空 ramdisk）。

### 9.2 刷 Ubuntu rootfs（P0）

```bash
fastboot flash userdata out/rootfs.ext4   # ⚠️ 清空 userdata
```

### 9.3 完整刷机流程（P0）

```bash
./scripts/make-empty-dtbo.sh
./scripts/build-bootimg.sh                # 含 initramfs 后
fastboot flash dtbo out/dtbo-empty.img
fastboot flash boot out/boot.img
fastboot flash vbmeta --disable-verification out/vbmeta.img
fastboot reboot
```

串口应出现 **systemd** 日志，而非 `init: Init encountered errors`。

### 9.4 进系统后（P1+）

| 项目 | 说明 |
|------|------|
| 串口登录 | `ttyMSM0` 115200，root/ginkgo |
| USB 网络 | RNDIS 配置，便于 SSH |
| 显示 | **主线 DRM 已出图**（2026-08-17） |
| WiFi | 移植 `wifi@c800000` 节点，ath10k 固件已在 rootfs |

---

## 10. 恢复 Android

```bash
./scripts/restore-android.sh
# 或
fastboot flash boot backup/ginkgo/boot.img
fastboot flash dtbo backup/ginkgo/dtbo.img
fastboot flash vbmeta backup/ginkgo/vbmeta.img
fastboot reboot
```

若 userdata 已被 Ubuntu 覆盖，需在 recovery 中 wipe data 或 `fastboot -w`。

---

## 11. 相关文档

| 文档 | 内容 |
|------|------|
| [`ginkgo-usb-ttl-uart.md`](ginkgo-usb-ttl-uart.md) | 接线图与测试点 |
| [`uart-debug-ginkgo.md`](uart-debug-ginkgo.md) | 串口抓 log 流程 |
| [`mainline-boot-failure-analysis.md`](mainline-boot-failure-analysis.md) | 2026-08-05 早期 fastboot 分析（已被本文 §5.1 覆盖） |
| [`mainline-ginkgo-porting-guide.md`](mainline-ginkgo-porting-guide.md) | 长期移植路线图 |
| [`backup/ginkgo/DEBUG.md`](../backup/ginkgo/DEBUG.md) | pstore / 无串口时的排障 |
| [`README.md`](../README.md) | 构建与刷机快速入门 |

---

## 12. 关键结论（一句话）

**2026-08-07 当晚：通过 USB-TTL 定位并修复 DTBO overlay、TLMM `gpio-reserved-ranges` 两个问题后，主线内核已能在 ginkgo 上完整启动；当前阻塞点是缺少 initramfs 与 Ubuntu rootfs，导致 Bootloader 仍拉起 Android init 并重启。**