# ginkgo-mainline-linux

**语言：** [English](README.md) | 简体中文

小米 **Redmi Note 8**（代号 **ginkgo**，高通 **SM6125**）的主线 Linux 适配仓库。

本仓库编译 Linux 7.0 + Ubuntu 26.04 arm64 rootfs，并刷到手机上。**天马 NT36672A** 真机上，显示、触控、Wi-Fi、GNOME、Adreno 610、Docker 均已打通。默认 `boot.img` 就是这份天马包：DSI 为 `power mode 0x9c` / `LANE_STATUS 0x1f00`，fbcon 会把内核日志打到屏上；Type-C 默认仍是 USB gadget（RNDIS/SSH），要用键鼠再切 host。

| 阶段 | 目标 | 状态 |
|------|------|------|
| P0 | 串口启动日志 | 部分（`ttyMSM0` 仍 deferred） |
| P1 | eMMC rootfs + systemd | 已完成 |
| P2 | USB RNDIS + SSH | 已完成 |
| P3 | DRM / DSI 显示 + fbcon | 已完成 |
| P4 | SPI 触控（NT36672A） | 已完成 |
| P5 | Wi-Fi（WCN3990 / ath10k_snoc） | 已完成 |
| P6 | Ubuntu GNOME + Adreno 610 | 已完成 |
| P7 | Docker Engine | 已完成 |

**刷 Release 镜像：** [中文教程](docs/zh-CN/flash-guide.md) · [English](docs/flash-guide.md)

硬件说明、UART 接线和完整 bring-up 记录在 [`docs/zh-CN/`](docs/zh-CN/README.md)。英文默认文档在 [`docs/`](docs/README.md)。

## 面板批次（天马 / 华星）

ginkgo 出厂有不止一种屏。DSI 接口相同，面板 IC 和 init 序列不同。

| 镜像 | 面板 | 状态 |
|------|------|------|
| `boot.img`（默认） | **天马 NT36672A** 1080×2340 | 已支持。显示、fbcon、SPI 触控、GNOME 桌面。 |
| `boot-huaxing.img` | **华星 / CSOT FT8719** | 已能出图（pclk 锁定为天马已验证的 183012 kHz）。触控仍关闭。不是已支持的桌面镜像。 |

华星机刷天马 `boot.img` 会黑屏，请改刷 `boot-huaxing.img`（建议先 `fastboot boot` 试一次再写分区）。

Type-C 默认是 USB gadget，保证 SSH/RNDIS。键鼠需要 OTG 转接，在手机上执行：

```bash
ginkgo-usb-host.sh
```

SSH 会立刻断开，重启（或把 role 改回 `device` 并重启 `usb-gadget-rndis`）后才能再连。

原版安卓上可以确认自己是哪块屏：

```bash
dmesg | grep -iE 'tianma|huaxing|ft8719|nt36672a|TP info'
```

若不能正常显示，请开 [GitHub Issue](https://github.com/Huabin1010/ginkgo-mainline-linux/issues/new)，附上 **启动日志**（从开机到 DRM/面板 probe）。有这份就够，不必整段聊天截图。

**串口（黑屏时最有用）。** 只用 1.8V。手机 TX **TP0003** → 转接板 RX，手机 RX **TP0012** → 转接板 TX，再接 GND。先开抓 log 再开机。接线见 [UART 指南](docs/zh-CN/ginkgo-usb-ttl-uart.md)。

```bash
picocom -b 115200 /dev/ttyUSB0 | tee uart.log
```

**USB SSH（内核已经进到用户态时）。** USB RNDIS 之后 `root@192.168.7.2`。仓库里没有 root 密码。

```bash
ssh root@192.168.7.2 'dmesg' > dmesg.txt
```

把 `uart.log` / `dmesg.txt` 贴进 Issue。下面这几行最关键：

```text
panel init complete
power mode readback
dsi_err
```

## 快速开始

```bash
# 1. 安装依赖（需 sudo，一次性）
./scripts/setup-deps.sh

# 2. 拉取内核源码（一次性）
./scripts/setup-kernel.sh

# 3. 编译内核 + ginkgo DTB
./scripts/build-kernel.sh

# 4. 打包 boot.img
./scripts/build-bootimg.sh

# 5. 制作 Ubuntu rootfs
./scripts/build-rootfs.sh
./scripts/configure-rootfs.sh
./scripts/build-rootfs-image.sh

# 6. 刷机（设备进入 fastboot）
./scripts/flash-boot.sh
# 仅首次需要，会清空 userdata：
# fastboot flash userdata out/rootfs.ext4
```

内核源码（`linux/`）和构建产物（`out/`）不入库，用上面的脚本拉取并编译。

## 刷机

**顺序：** 先刷 rootfs，再刷 boot，最后禁用 vbmeta 验证。

```bash
export PATH="$HOME/.local/bin:$PATH"

# 1. 进入 fastboot（adb reboot bootloader 或按键组合）

# 2. 刷 Ubuntu rootfs 到 userdata（会清空该分区）
fastboot flash userdata out/rootfs.ext4

# 3. 刷主线内核
fastboot flash boot out/boot.img

# 4. 禁用 verified boot
fastboot flash vbmeta --disable-verification vbmeta.img

# 5. 重启
fastboot reboot
```

登录：串口 `ttyMSM0`，或 USB RNDIS SSH（`root@192.168.7.2`）。**仓库里没有 root 密码。** 写到已忽略的 `rootfs-overlay/etc/ginkgo-root-password`，或导出 `GINKGO_ROOT_PASSWORD`。

串口：`ttyMSM0,115200n8` @ `0x4a90000`（**只用 1.8V**）。接线见 [UART 指南](docs/zh-CN/ginkgo-usb-ttl-uart.md)。

## 目录结构

```
config/           内核 config fragment
scripts/          构建、刷机、UART、主机 USB 脚本
docs/             英文技术文档（默认）
docs/zh-CN/       中文原稿
firmware/ginkgo/  从设备提取的固件
reference/        下游 / 主线 DTS 与驱动摘录
rootfs-overlay/   写入 Ubuntu 镜像的文件
backup/ginkgo/    分区表与恢复说明（镜像文件不入库）
host/             主机 NetworkManager USB RNDIS 配置
```

## 文档

| 主题 | 中文 | English |
|------|------|---------|
| 刷 Release | [刷机教程](docs/zh-CN/flash-guide.md) | [flash guide](docs/flash-guide.md) |
| 文档目录 | [docs/zh-CN/README.md](docs/zh-CN/README.md) | [docs/README.md](docs/README.md) |
| 全记录 | [bring-up 编年](docs/zh-CN/ginkgo-mainline-bringup-chronicle.md) | [EN](docs/ginkgo-mainline-bringup-chronicle.md) |
| 技术手册 | [移植手册](docs/zh-CN/mainline-ginkgo-porting-guide.md) | [EN](docs/mainline-ginkgo-porting-guide.md) |
| 显示 | [2026-08-17](docs/zh-CN/ginkgo-display-complete-2026-08-17.md) | [EN](docs/ginkgo-display-complete-2026-08-17.md) |
| fbcon | [2026-08-18](docs/zh-CN/ginkgo-fbcon-boot-2026-08-18.md) | [EN](docs/ginkgo-fbcon-boot-2026-08-18.md) |
| 触控 | [2026-08-17](docs/zh-CN/ginkgo-touch-complete-2026-08-17.md) | [EN](docs/ginkgo-touch-complete-2026-08-17.md) |
| Wi-Fi | [2026-08-18](docs/zh-CN/ginkgo-wifi-complete-2026-08-18.md) | [EN](docs/ginkgo-wifi-complete-2026-08-18.md) |
| 桌面 / GPU | [Ubuntu](docs/zh-CN/ginkgo-ubuntu-desktop-2026-08-19.md) · [Adreno 610](docs/zh-CN/ginkgo-gpu-desktop-2026-08-19.md) | [EN](docs/ginkgo-ubuntu-desktop-2026-08-19.md) |
| Docker | [2026-08-19](docs/zh-CN/ginkgo-docker-2026-08-19.md) | [EN](docs/ginkgo-docker-2026-08-19.md) |

## 环境变量

见 `scripts/env.sh`：

| 变量 | 默认 | 说明 |
|------|------|------|
| `KERNEL_TAG` | v7.0 | 内核版本 |
| `KBUILD_OUTPUT` | `out/kernel` | 构建输出目录 |
| `JOBS` | `nproc` | 并行编译数 |
| `GINKGO_ROOT_PASSWORD` | （未设置） | 镜像 root 密码；否则读 overlay 文件 |

## 许可证

本仓库的构建脚本和原创文档使用 [GPL-2.0-only](LICENSE)，与 Linux 内核一致。`reference/` 下的下游摘录保留其上游许可证。设备固件为专有二进制，仅供在提取它的那台设备上运行 Linux。
