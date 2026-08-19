**语言：** [English](../ginkgo-ubuntu-desktop-2026-08-19.md) | 简体中文

# Redmi Note 8 (ginkgo) Ubuntu 桌面：清华源、完整 GNOME 装上，会话已起、屏幕仍黑

> **后续（同日稍后）：** 原生 Adreno 610 已起来，用户确认进入桌面。黑屏原因和 GPU 适配见 [ginkgo-gpu-desktop-2026-08-19.md](./ginkgo-gpu-desktop-2026-08-19.md)。本文保留「软件装上、当时黑屏」的过程，不要按第 3 节的 swrast 权宜之计去改现在这台机。

> 设备：Xiaomi Redmi Note 8 · codename **ginkgo** · SoC **SM6125 (trinket)** · 序列号 `<serial>`  
> 系统：主线 Linux 7.0 + **Ubuntu 26.04 LTS (resolute)** arm64，rootfs 在 userdata（`mmcblk0p87`，约 49 G）  
> 验收日：2026-08-19 凌晨  
> 前置：显示 P3、fbcon 启动日志、触控 P4、WiFi P5 均已通。

**关联文档 / skill**

| 文档 | 内容 |
|------|------|
| [ginkgo-display-complete-2026-08-17.md](./ginkgo-display-complete-2026-08-17.md) | DPU→DSI 出图（像素链路） |
| [ginkgo-fbcon-boot-2026-08-18.md](./ginkgo-fbcon-boot-2026-08-18.md) | 内核启动日志上屏 |
| [ginkgo-wifi-complete-2026-08-18.md](./ginkgo-wifi-complete-2026-08-18.md) | WCN3990 上网（本次 apt 的前提） |
| [ginkgo-mainline-bringup-chronicle.md](./ginkgo-mainline-bringup-chronicle.md) | 全机 bring-up 时间线 |
| [usb-connect.sh](../scripts/usb-connect.sh) | USB RNDIS SSH |
| [reboot-fastboot.sh](../scripts/reboot-fastboot.sh) | 从 Ubuntu 进 fastboot |

验证内核仍用 `fastboot boot out/boot.img`，**不要** `fastboot flash boot`。桌面装在 **userdata 的 rootfs** 上，不写 boot 分区。

---

## 0. 一句话结论（当前成果 + 边界）

这次做的不是再修 DSI，而是把最小 rootfs 变成 **能自动登录的 Ubuntu GNOME 桌面栈**。已经落地的：

1. **apt 换成清华 TUNA `ubuntu-ports`**（含 universe / updates / security）。本机访问不了 `ports.ubuntu.com`。  
2. **`ubuntu-desktop` 完整元包已装上**（约 1277 个新包，下载 718 MB / 安装约 3 GB）。  
3. **`gdm3` + `gnome-shell --mode=ubuntu` 已在跑**，用户 `ginkgo` 自动登录到 `seat0` / `tty2`。  
4. **可以用家里 WiFi SSH**：`ssh root@<wlan0-dhcp>` 或 `ssh ginkgo@<wlan0-dhcp>`，密码 `$GINKGO_ROOT_PASSWORD`。USB `192.168.7.2` 仍可用。

还没落地的（用户看见的是 **黑屏**）：

- **没有原生 Adreno 3D GPU。** `/dev/dri/card0` 是 **DPU 显示控制器**，不是 GPU。  
- Mesa 看到这块 DRM 节点就去加载 **`msm_dri`**，日志：`egl: failed to create dri2 screen`，然后 mutter 仍建了 GBM renderer。  
- `gnome-shell` 占满约 **一核 CPU（~102%）**、RSS ~480 MB，会话注册成功，但 **合成器没把可见帧送到面板**。  
- 这不是「没装好桌面」，也不是「DSI 又坏了」（`dpms=On`，`fb0/blank=0`，connector `enabled`）。

下一步应先 **逼 Mesa 走 `kms_swrast` / 软件 GL，必要时关掉 Wayland 改 Xorg**，不要一上来就整机重启。Adreno 610 主线 GPU 是另一条 bring-up。

---

## 1. 现在机器上有什么

| 项 | 状态 |
|----|------|
| Ubuntu | 26.04 LTS resolute，`LANG=zh_CN.UTF-8` |
| 磁盘 | userdata ~49 G，装完桌面后仍很空 |
| 内存 | 5.4 GiB，无 swap |
| CPU | Kryo 260 8 核；空闲 load ~1.3；GNOME 起来后 gnome-shell ~100%（软件渲染） |
| 显示链路 | DPU/DSI 仍健康；fbcon 仍可在无 GNOME 时出内核日志 |
| 触控 / WiFi | 保持 P4 / P5，未为桌面去改驱动 |
| 3D GPU | **未启用**（无 `gpu@` 节点、无 Adreno/zap 固件） |
| 用户 | `ginkgo`（uid 1000），sudo，密码 `$GINKGO_ROOT_PASSWORD`；root 同样 `$GINKGO_ROOT_PASSWORD` |
| 自动登录 | `/etc/gdm3/custom.conf`：`AutomaticLogin=ginkgo`，`WaylandEnable=true` |
| 默认目标 | `graphical.target`；`gdm.service` enabled |

软件确认（2026-08-19 01:15 之后）：

```
ii  gdm3                   50.1-0ubuntu0.1
ii  ubuntu-desktop         1.570.2
ii  gnome-shell            50.1-0ubuntu1.2
gnome-shell --mode=ubuntu
gdm-wayland-session /usr/bin/gnome-session --session=ubuntu
```

---

## 2. 做了哪些事（按时间）

### 2.1 清华源

实机原来只有：

```
deb http://ports.ubuntu.com/ubuntu-ports resolute main
```

且 `ports.ubuntu.com` ping 不通。TUNA HTTP 可通（~45–70 ms）。

落地文件：

- 实机 + overlay：`rootfs-overlay/etc/apt/sources.list.d/ubuntu.sources`  
- overlay：`rootfs-overlay/etc/apt/sources.list`（只留注释，避免和 DEB822 重复）  
- 以后新 rootfs：`scripts/build-rootfs.sh` 的 `MIRROR` 默认 TUNA

security 也走 TUNA：官方 security 源这台机访问不到。TUNA 文档不建议换 security，但这里没有别的选择。

HTTPS 第一次失败：手机 RTC 停在 **2026-04-15**，Release/证书都「尚未生效」。校时到 2026-08-18 之后 HTTP `apt update` 正常。无 RTC，重启后时钟还会漂，桌面里已装 `systemd-timesyncd`，联网后应能 NTP。

### 2.2 完整 `ubuntu-desktop`

不是 `ubuntu-desktop-minimal`。清华源实测：

- 要下载 **726 MB / 仓库 853 MB**（部分已在缓存）  
- 安装后约 **3.0 GB**  
- WiFi `<test-ap-5g>`，空口 433 Mbit/s VHT80；apt 平均 **~9.7 MB/s**（718 MB / 74 s），对一堆小 deb 来说正常  

日志：`/tmp/ubuntu-desktop-install.log`。`INSTALL_EXIT:0` 出现在 **2026-08-19 01:06**。解包相对快，**`dpkg --configure` 最慢**（一千多个包串行跑 postinst：ldconfig、字体、systemd），大约 40–50 分钟。

另外给中文环境装了 `fonts-noto-cjk`、`ibus-libpinyin`、`locales`。

`btop` / `htop`：桌面 apt 锁着时已排队，桌面结束后应能装上；以机上 `dpkg -l btop htop` 为准。

### 2.3 装完为什么还不进桌面

`apt` 是 **系统已经 boot 完之后** 才装完的。`graphical.target` 开机时就已经到达，当时还没有 gdm，systemd **不会回头再启动显示管理器**。所以屏幕一直停在 fbcon。

更糟的是：`gdm3` / `ubuntu-desktop` 当时是 **`iU`（解包未配置）**。

| 缺失 | 现象 |
|------|------|
| 系统用户 `gdm` 未创建（`systemd-sysusers gdm3.conf` 没跑成） | `generate-config`: `install: invalid user: 'gdm'`，gdm 起不来 |
| `/etc/pam.d/gdm-autologin` 等还停在 `*.dpkg-new` | 自动登录 PAM 失败：`auth could not identify password for [ginkgo]` |
| overlay 里已有 `custom.conf` | dpkg 配 gdm3 时会问 conffile；必须 `--force-confold`，否则交互 EOF 再次配失败 |

处理：

```
systemd-sysusers gdm3.conf          # 建 gdm 用户
把 gdm-*.dpkg-new 就位
DEBIAN_FRONTEND=noninteractive dpkg --force-confold --configure gdm3
dpkg --configure ubuntu-desktop-minimal ubuntu-desktop
保留 overlay 的 AutomaticLogin=ginkgo
systemctl start/restart gdm.service
```

之后：`session opened for user ginkgo`，`gnome-shell --mode=ubuntu`，`GNOME Shell started`。

### 2.4 WiFi SSH

手机 DHCP：`wlan0` **`<wlan0-dhcp>/24`**，主机有线 `<host-lan>` 同网段。sshd 听 `0.0.0.0:22`，`PermitRootLogin yes`。

```bash
ssh root@<wlan0-dhcp>      # 不要加 -b 192.168.7.1
ssh ginkgo@<wlan0-dhcp>
# 密码见 overlay
```

IP 是 DHCP，下次可能变。不确定时用 USB SSH 看 `ip -4 addr show wlan0`。

---

## 3. 黑屏根因（已经定位，尚未改渲染路径）

GNOME **进程层是成功的**。黑的是 **3D/合成路径**：

```
gnome-shell 日志：
  libEGL warning: egl: failed to create dri2 screen
  Failed to initialize accelerated iGPU/dGPU framebuffer sharing: Not hardware accelerated
  Created gbm renderer for '/dev/dri/card0'

ls /usr/lib/aarch64-linux-gnu/dri | grep -E 'msm|swrast'
  kms_swrast_dri.so
  msm_dri.so
  swrast_dri.so
```

`card0` 的 driver 是 **`msm_dpu`**（`display-controller@5e01000`），不是 Adreno。Mesa 仍优先 `msm_dri`。DTS 里没有 `gpu@`，rootfs 也没有 Adreno/zap 固件。

当前 GNOME 环境：`XDG_SESSION_TYPE=wayland`，`GSK_RENDERER=ngl`（overlay `99-ginkgo-gnome.conf`）。ngl + 失败的 msm EGL + 1080×2340 软件尝试 → 一核打满、看不见第一帧。

**不要**把这次黑屏当成 P3 显示回归。分层：

| 层 | 现在 |
|----|------|
| L1–L3 面板/DSI/DPU | 通 |
| L4 fbdev/DPMS | `blank=0`，`dpms=On` |
| L5 GNOME/Wayland/EGL | **失败可见输出** |

建议的下一刀（还没做）：

1. `MESA_LOADER_DRIVER_OVERRIDE=kms_swrast`（或 `LIBGL_ALWAYS_SOFTWARE=1`）  
2. `GSK_RENDERER=cairo`（软件路径比 ngl 稳）  
3. 仍黑再 `WaylandEnable=false`，走 Xorg + llvmpipe  
4. **先 `systemctl restart gdm`，不要整机 reboot**（无 RTC、要重配 USB SSH）  
5. Adreno 610 主线 GPU：DT 节点 + 固件，另开任务

---

## 4. 仓库里对应改动

| 路径 | 作用 |
|------|------|
| `rootfs-overlay/etc/apt/sources.list.d/ubuntu.sources` | TUNA DEB822 |
| `rootfs-overlay/etc/apt/sources.list` | 清空旧 one-line，避免重复 |
| `scripts/build-rootfs.sh` | 新 debootstrap 默认 TUNA |
| `rootfs-overlay/etc/gdm3/custom.conf` | ginkgo 自动登录、Wayland |
| `rootfs-overlay/etc/dconf/db/local.d/00-ginkgo-desktop` | 屏幕键盘、缩放 2、禁止闲置休眠 |
| `rootfs-overlay/etc/environment.d/99-ginkgo-gnome.conf` | `GSK_RENDERER=ngl`（**黑屏后应改**） |
| `rootfs-overlay/usr/local/sbin/display-unblank.sh` | 只 unblank，不画测试图（避免打桌面） |

initramfs overlay 每次启动会再铺一层；`ubuntu.sources` 和 `custom.conf` 会跟着回来。

---

## 5. 不要做

| 不要 | 原因 |
|------|------|
| 黑屏先 reboot | 时钟丢失、USB 要重配；渲染问题 reboot 还会黑 |
| 回退 simplefb / 改 DSI 预取来「救桌面」 | 像素链路是好的 |
| 在 `msm_fbdev` probe 里 `restore_fbdev_mode()` | ginkgo 实测卡死 |
| `fastboot flash boot` | 本仓库约定只 `fastboot boot` |
| 为桌面去 bounce `wlan0` / 改 MAC | 会搞崩 WLAN.HL.3.x |
| 把 `msm_dri` 失败当成「没装 GNOME」 | 包和会话都在 |

---

## 6. 和其它完成文档的边界

| 问题 | 文档 |
|------|------|
| 有背光无图像、FIFO、INTF 预取 | [显示出图](./ginkgo-display-complete-2026-08-17.md) |
| 内核阶段无启动日志 | [fbcon](./ginkgo-fbcon-boot-2026-08-18.md) |
| 扫网 / 关联 / 速率 | [WiFi](./ginkgo-wifi-complete-2026-08-18.md) |
| 清华源、完整桌面、gdm、当时黑屏（无 GPU） | **本文**（过程） |
| Adreno 610、桌面可见、稍微有点卡 | [GPU 桌面](./ginkgo-gpu-desktop-2026-08-19.md) |

---

## 7. 回归命令

```bash
# WiFi（IP 以实机为准）
ssh ginkgo@<wlan0-dhcp>

# 会话是否在
loginctl
pgrep -a gnome-shell
systemctl is-active gdm.service

# 是不是又在用坏掉的 msm EGL
journalctl _UID=1000 --since "5 min ago" | grep -E "dri2 screen|llvmpipe|kms_swrast|GNOME Shell started"
```
