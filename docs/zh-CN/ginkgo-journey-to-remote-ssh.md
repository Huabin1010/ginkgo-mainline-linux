**语言：** [English](../ginkgo-journey-to-remote-ssh.md) | 简体中文

# Redmi Note 8 (ginkgo) 主线 Linux 适配心路历程

> 设备：Xiaomi Redmi Note 8（`ginkgo`，序列号 `<serial>`）  
> 平台：Qualcomm SM6125  
> 目标：主线 Linux + Ubuntu 26.04，通过 USB 远程 SSH 调试  
> 文档日期：2026-08-08  
> 里程碑：**USB RNDIS + SSH 已打通**（`ssh root@192.168.7.2`，密码见 `root-password.md`）  
> 后续：**显示 2026-08-17 出图**，**触控 2026-08-17 出点** —— 见 [ginkgo-display-complete-2026-08-17.md](ginkgo-display-complete-2026-08-17.md)、[ginkgo-touch-complete-2026-08-17.md](ginkgo-touch-complete-2026-08-17.md)

本文不是技术手册，而是一份**从「什么都看不见」到「PC 上能 SSH 进手机」**的完整经历记录：我们踩过什么坑、靠什么手段撑过来、以及每个阶段心里在想什么。

更细的内核 panic 分析见 [`ginkgo-bringup-journal.md`](ginkgo-bringup-journal.md)；接线与采购见 [`ginkgo-usb-ttl-uart.md`](ginkgo-usb-ttl-uart.md)。

---

## 0. 我们到底在干什么

一句话：**在 Redmi Note 8 上跑主线 Linux，而不是 Lineage/Android。**

这不是刷个 Magisk 模块，而是从零搭一条链路：

```
主线内核 boot.img
    → initramfs 挂载 userdata 上的 Ubuntu rootfs
    → systemd 正常启动
    → USB RNDIS 虚拟网卡
    → PC 端 SSH 远程登录
```

屏幕黑着、WiFi 没有、触控没有——在这些都还没搞定之前，我们必须先有一条**不依赖屏幕的调试通道**。这条通道的终点，就是 2026-08-08 凌晨验证成功的 `ssh root@192.168.7.2`。

---

## 1. 第一阶段：黑暗中摸索（2026-08-05）

### 1.1 刷机即 fastboot，毫无反馈

最早的状态极其挫败：把编好的 `boot.img` 刷进去，手机亮一下，几秒后回到 fastboot。没有 adb，没有画面，没有任何可读输出。

当时能确定的只有配置层面的嫌疑：

| 问题 | 后果 |
|------|------|
| `CONFIG_ARM64_VA_BITS_52` | Kryo 260 不支持 52-bit VA |
| `qcom,board-id` 写成 `0x16` | 本机实际是 `0x22`，ABL 可能拒载 |
| `CONFIG_SM_GCC_6125` 未开 | 全局时钟没起来，外设全挂 |
| boot.img 打包格式不对 | header v2 + 独立 DTB 需对齐原厂 |

这些一项项修掉，**依然回 fastboot**。连续 24 次测试，日志里只有冷冰冰的一行：`adb=none fb=<serial> fastboot`。

那时最大的感受是：**我们在盲打。** 内核有没有跑起来？ABL 在哪一步拒了？完全不知道。

### 1.2 意识到：没有串口，等于没有眼睛

项目 cmdline 里早就写了：

```
console=ttyMSM0,115200n8
earlycon=qcom_geni,0x4a90000
```

但没有物理 UART 线，这些参数等于没写。屏幕在 early boot 阶段通常也不亮，adb 更不可能起来。

于是开始认真调研 **USB-TTL 方案**——这是整个项目从「瞎猜」转向「可观测」的转折点。

---

## 2. 第二阶段：焊上眼睛（2026-08-05 ~ 08-07）

### 2.1 找测试点：差点焊错地方

网上大量教程红框标注的是 **EDL 测试点**（短接进 9008 刷机），**不是** debug UART。如果按「左边 TX、右边 RX」去焊 EDL 点，除了能进 EDL，永远看不到 boot log。

真正的 UART 在 LLDM516 原理图里：

| 测试点 | 信号 | 说明 |
|--------|------|------|
| **TP0003** | 手机 TX (GPIO16) | 接模块 RX |
| **TP0012** | 手机 RX (GPIO17) | 接模块 TX |
| **GND** | 地 | 螺丝孔或大面积地 |

焊盘只有 **0.3~0.8 mm**，杜邦线手扶根本不稳。最终用漆包线焊上：

- 黄色 → TP0003（手机 TX）
- 绿色 → TP0012（手机 RX）
- 橙色 → GND

电平必须是 **1.8V**。模块拨错到 3.3V/5V，轻则读不到，重则伤 SoC。

### 2.2 串口终于有字了

插上 CH340（`1a86:55d3`），`python3 scripts/uart-monitor.py` 跑起来，再开机——**第一次看见 ABL 和内核的 printk**。

空日志、乱码、波特率不对，都经历过。但 2026-08-07 23:00 之后，日志终于能稳定落盘到 `backup/ginkgo/logs/`。

**这一刻的意义：** 从此每个 panic、每次 fastboot、每条 systemd 日志都有据可查。后面所有修复，都建立在串口之上。

---

## 3. 第三阶段：内核过关（2026-08-07 深夜）

### 3.1 DTBO overlay：ABL 在进内核前就拦住了

第一份有效 UART 日志（`uart-20260807-233001.log`）：

```
ApplyOverlay: ufdt apply overlay failed
Error: Dtb overlay failed
Launching fastboot
```

原厂 `dtbo.img` 的 overlay 和主线 DTB 结构不兼容。修复：**刷入 24MB 全零 `dtbo-empty.img`**，让 ABL 跳过 overlay，只用 boot.img 内嵌 DTB。

### 3.2 TLMM panic：5.76 秒必死

绕过 DTBO 后，内核启动，约 5.76 秒：

```
SError Interrupt on CPU5
pc : gpiochip_add_data_with_key
Kernel panic - not syncing
sm6125_tlmm_probe
```

曾怀疑 `soc@0` 地址格式错了，改成 2-cell 后 **panic 依旧**。用 `fdtget` 核对 TLMM `reg` 正确，反汇编定位到在读 **GPIO0** 方向寄存器时 fault。

根因：`gpio-reserved-ranges` 写错，内核去碰了 QUP 专用引脚：

```dts
/* 错误 */
gpio-reserved-ranges = <22 2>, <28 6>;

/* 正确 */
gpio-reserved-ranges = <0 4>, <30 4>;
```

修完后，日志里第一次出现：

```
sm6125_tlmm_init returned 0
mmcblk1: mmc1:0001 3H6CAB 58.2 GiB
printk: legacy console [ttyMSM0] enabled
Run /init as init process
```

**内核，活了。**

### 3.3 Android init 把我们踢出去

但用户态跑的是 **Android `/init`**，不是 Ubuntu。Bootloader 在 cmdline 末尾追加：

```
skip_initramfs rootwait ro init=/init
```

覆盖了我们的 `init=/sbin/init`。Android init 挂载 selinuxfs 失败、等 mmc0 SD 卡超时，然后：

```
Init encountered errors starting first stage, aborting
reboot: Restarting system with command 'bootloader'
```

内核能启动，却进不了自己的系统——这是第三阶段结束时最憋屈的地方。

---

## 4. 第四阶段：把 Linux 真正装进手机（2026-08-07 ~ 08-08）

### 4.1 initramfs：夺回 init 控制权

编了静态 aarch64 `/init`（`initramfs/init.c`），在 ramdisk 里：

1. 挂载 proc/sys/dev
2. 等待并 mount userdata（ext4）
3. 部署 `rootfs-overlay/` 到 `/newroot`
4. `switch_root` 执行 `/sbin/init`

这样即使 bootloader 追加 `init=/init`，ramdisk 里的 `/init` 也会先跑起来，把根切到 Ubuntu。

### 4.2 Ubuntu rootfs 上 userdata

`scripts/build-rootfs.sh` debootstrap 出 Ubuntu 26.04 arm64 minimal，刷入 userdata 分区（`mmcblk0p87`）。串口第一次看到 **systemd** 日志，而不是 `init: Init encountered errors`。

### 4.3 recovery 挂不了 userdata 的插曲

曾尝试在 TWRP recovery 里直接挂载 userdata 增量更新 overlay，失败原因是 userdata 的 ext4 用了较新的 `metadata_csum_seed`，recovery 内核太旧挂不上。

**变通：** 把 `rootfs-overlay/` 打进 **initramfs**，每次启动由 `/init` 自动部署到 userdata。只刷 `boot.img` 就能更新用户态脚本，不必重刷 2GB rootfs。

---

## 5. 第五阶段：USB 远程调试——最长的一条链（2026-08-08）

目标：PC 用一根 micro-USB 数据线（一直插着的那条）就能 `ssh` 进手机，不再依赖焊死的 TTL 线。

### 5.1 第一层：dwc3 起不来

串口：

```
platform 4e00000.usb: deferred probe pending
dwc3: failed to initialize core
```

| 问题 | 修复 |
|------|------|
| `sm6125.dtsi` usb3 地址格式 | 改 2-cell + 补 `resets` |
| PHY compatible 错误 | `msm8996-qusb2-phy` → `qcom,sm6125-qusb2-phy` |
| dwc3-qcom interconnect 失败 | 无 interconnect 时跳过（`-ENODEV`） |
| **PHY 驱动未编进内核** | `CONFIG_PHY_QCOM_QUSB2=m` → **`=y`** |

最后一项是关键：`CONFIG_PHY_QCOM_QUSB2=m` 时模块根本没加载，PHY 从未 probe，dwc3 一直 deferred。

### 5.2 第二层：RNDIS 服务被拖死 90 秒

USB 硬件修好后，PC 仍看不到网卡。串口显示 `usb-gadget-rndis.service` 在等 `local-fs.target`，而 `local-fs` 被 **persist 分区挂载** 卡住（80+ 个 mmc 分区 udev 处理极慢）。

曾误以为「没插 USB 数据线」——实际上 recovery adb 一直能用，线是插着的；是 **gadget 服务根本没执行**。

修复：

- `usb-gadget-rndis.service` 去掉 `After=local-fs.target`，改 `WantedBy=sysinit.target`
- **persist 移出 fstab**，改为 `persist-mount.service` 延迟挂载

### 5.3 第三层：emergency mode 挡住 SSH

persist 在 fstab 里时，systemd 默认等 `by-partlabel/persist` 设备 **90 秒**超时 → `local-fs.target` 失败 → **emergency mode** → `ssh.service` 从未启动。

串口停在：

```
Enter root password for system maintenance
(or press Control-D to continue):
```

USB RNDIS 能 ping 通 `192.168.7.2`，但 `ssh` 报 `Connection refused`。

修复后串口出现：

```
Reached target multi-user.target
Started ssh.service
ginkgo login:
```

### 5.4 第四层：密码对了也登不进 SSH

SSH 端口开放后，`ginkgo` 和 `$GINKGO_ROOT_PASSWORD` 都 `Permission denied`。用户并未改过密码，容易误判为「密码丢了」。

真正原因：**Ubuntu 26.04 / OpenSSH 默认 `PermitRootLogin prohibit-password`**——禁止 root 用密码登录，只允许公钥。串口本地登录不受影响，SSH 却一律拒绝。

修复：

- `/etc/ssh/sshd_config.d/99-ginkgo.conf`：`PermitRootLogin yes`
- `ensure-root-password.service`：从 `/etc/ginkgo-root-password` 同步 root 密码为 `$GINKGO_ROOT_PASSWORD`

---

## 6. 终章：连上了（2026-08-08 01:20）

recovery 刷入最新 `boot.img` 后，PC 端：

```
PING 192.168.7.2 — 0% 丢包
ssh root@192.168.7.2
```

```text
SSH_OK
ginkgo
uptime: up 2 min
ssh.service: active
PermitRootLogin yes
```

`lsusb` 可见 `1d6b:0104 Linux Foundation Multifunction Composite Gadget`。

**这一刻，项目有了第一条不依赖串口焊线的远程调试通道。**

---

## 7. 两条 USB 线，各干各的

后期才彻底理清（曾搞混过）：

| 连接 | 设备 | 作用 |
|------|------|------|
| 手机 **micro-USB** → PC | `1d6b:0104` RNDIS | 网络 + SSH + recovery adb |
| **TTL 测试点** → CH340 | `1a86:55d3` 串口 | 仅启动日志，与 USB 数据无关 |

micro-USB 数据线**一直插着**；PC 上看不到手机，不是线没插，是 **gadget 服务没跑起来**。

---

## 8. 修复链总览（从 blind 到 SSH）

```
[看不见] fastboot 循环
    ↓ USB-TTL 焊线 + uart-monitor
[看得见] ABL / 内核日志
    ↓ 空 dtbo + gpio-reserved-ranges
[内核活] TLMM / eMMC / ttyMSM0
    ↓ initramfs + Ubuntu rootfs
[用户态] systemd 启动
    ↓ DTS/驱动/CONFIG_PHY_QCOM_QUSB2=y
[USB 硬件] dwc3 + qusb2 phy probe 成功
    ↓ gadget 早启动 + persist 移出 fstab
[USB 网络] RNDIS 192.168.7.2
    ↓ PermitRootLogin + ensure-root-password
[远程调试] ssh root@192.168.7.2 ✓
```

---

## 9. 关键文件与脚本（后人用）

| 路径 | 作用 |
|------|------|
| `out/boot.img` | 含 initramfs overlay 部署 + USB/SSH 修复 |
| `config/ginkgo.fragment` | 内核 fragment（含 `CONFIG_PHY_QCOM_QUSB2=y`） |
| `rootfs-overlay/` | USB gadget、fstab、sshd、密码同步等 |
| `initramfs/init.c` | 挂载 userdata + 部署 overlay |
| `scripts/uart-monitor.py` | 串口监听 |
| `scripts/flash-linux-boot.sh` | recovery 刷 boot（`FLASH_ROOTFS=0` 只刷 boot） |
| `scripts/host-usb-connect.sh` | PC 配网并 SSH |
| `root-password.md` | root 密码 `$GINKGO_ROOT_PASSWORD` |

### PC 端日常连接

```bash
# 网卡出现后
nmcli connection up ginkgo-usb
# 或
./scripts/host-usb-connect.sh

ssh root@192.168.7.2    # 密码见 `$GINKGO_ROOT_PASSWORD`
```

### 只更新用户态 overlay（需 recovery 能挂 userdata 时）

```bash
./scripts/update-rootfs-via-recovery.sh
```

多数情况 **只刷 boot** 即可（overlay 在 initramfs 里）。

---

## 10. 还没做完的事

USB SSH 是里程碑，不是终点：

| 优先级 | 项目 | 现状 |
|--------|------|------|
| P2 | 屏幕（DRM/DSI） | **已打通**（2026-08-17，见 [出图全记录](./ginkgo-display-complete-2026-08-17.md)） |
| P3 | WiFi（ath10k + ICNSS） | 固件在 rootfs，节点待补 |
| P3 | 触控（SPI Novatek） | 无主线驱动 |
| — | 去掉 debug cmdline | `loglevel=8` 等非正式参数可收敛 |
| — | 串口仍建议保留 | 内核 panic 时 SSH 来不及 |

---

## 11. 几句心里话

这条路比想象中长很多。

最开始是 **24 次 fastboot 无人回应**；焊上串口后，是 **5.76 秒的 TLMM panic** 反复出现；内核进了，又被 **Android init 踢回 bootloader**；好不容易 systemd 起来，USB 又 **deferred probe 一整条链**；RNDIS ping 通了，**SSH 却 refused**；端口开了，密码 **Permission denied**——而密码其实从没改过，是 OpenSSH 默认不让 root 用密码登录。

每一个「明明就差一点」的背后，都是一层被挡住的依赖。串口让我们看见依赖；initramfs 让我们夺回系统；USB gadget 让我们摆脱焊线；sshd 配置让我们真正在 PC 上敲下第一行远程命令。

**2026-08-08 凌晨，`ssh root@192.168.7.2` 返回 `ginkgo`——这条线，算是接上了。**

---

## 12. 相关文档

| 文档 | 内容 |
|------|------|
| [`ginkgo-display-complete-2026-08-17.md`](ginkgo-display-complete-2026-08-17.md) | **显示出图全记录与心得** |
| [`ginkgo-bringup-journal.md`](ginkgo-bringup-journal.md) | 内核启动问题技术日志（至 Android init 阶段） |
| [`ginkgo-usb-ttl-uart.md`](ginkgo-usb-ttl-uart.md) | UART 接线、采购、焊接 |
| [`mainline-ginkgo-porting-guide.md`](mainline-ginkgo-porting-guide.md) | 长期移植路线图 |
| [`mainline-boot-failure-analysis.md`](mainline-boot-failure-analysis.md) | 2026-08-05 早期 fastboot 分析 |
| [`uart-debug-ginkgo.md`](uart-debug-ginkgo.md) | 串口抓 log 流程 |
| [`../README.md`](../README.md) | 构建与刷机快速入门 |
