**语言：** [English](../ginkgo-fbcon-boot-2026-08-18.md) | 简体中文

# Redmi Note 8 (ginkgo) 主线 fbcon：从「内核阶段黑屏」到启动日志上屏

> 设备：Xiaomi Redmi Note 8 · codename **ginkgo** · SoC **SM6125 (trinket)** · 序列号 `<serial>`  
> 面板：Tianma **NT36672A** · 1080×2340@60 · 主线 DRM / DPU / DSI（**不回退 simplefb**）  
> 验收日：2026-08-18 晚 · 用户确认启动时能看到 **Linux 内核日志**刷在屏幕上  
> 前置：同机显示 P3（2026-08-17 品红出图）已通。本文只解决「内核跑着但屏是黑的」。

**关联文档 / skill**

| 文档 | 内容 |
|------|------|
| [ginkgo-display-complete-2026-08-17.md](./ginkgo-display-complete-2026-08-17.md) | DPU→DSI 出图（本机前置；链路、FIFO、预取） |
| [ginkgo-wifi-complete-2026-08-18.md](./ginkgo-wifi-complete-2026-08-18.md) | 同日 WCN3990 关联（并行工作，与 fbcon 无关） |
| [ginkgo-display-bringup-methodology.md](./ginkgo-display-bringup-methodology.md) | DPMS / `fb0/blank` / `display-unblank.service` |
| [ginkgo-mainline-bringup-chronicle.md](./ginkgo-mainline-bringup-chronicle.md) | 全机 bring-up 时间线 |
| [display-bringup-loop.sh](../scripts/display-bringup-loop.sh) | 黑屏分层 SOP |
| [usb-connect.sh](../scripts/usb-connect.sh) | 每次 reboot 后重配 RNDIS |
| [reboot-fastboot.sh](../scripts/reboot-fastboot.sh) | 从 Ubuntu 进 fastboot |

验证用 `fastboot boot out/boot.img`，**不要** `fastboot flash boot`。

---

## 0. 一句话结论

内核启动那段能出字，不是因为「再修一轮 DSI」或「提前跑 systemd unblank」，而是因为：

1. **显示硬件在 P3 就已经通了。** 面板约 4–7 秒 `panel init complete`，DRM 立刻注册 `fb0`。黑的不是链路，是 **没有人往这块 fb 写内核日志，也没有人在内核里把 DPMS 打开**。  
2. **`CONFIG_FRAMEBUFFER_CONSOLE` 早就编进去了**，fbcon 甚至会绑定 VT。但 cmdline **没有** `console=tty0`，printk 只走 `ttyMSM0`，屏幕上没有控制台。  
3. 以前故意不加 `tty0`，注释写的是 **fbcon 和异步 SDHCI 打架**。那是 simplefb 时代（fb 很晚才注册、SD 卡槽还在 probe）。现在 eMMC ~2.5s 就好、`sdhc_2` 已禁用、DRM ~7s 才接管，这条旧禁令已经过时。  
4. 真正生效的 cmdline 在 **`scripts/build-bootimg.sh`**。Android boot.img 会盖掉 DT `chosen.bootargs`，只改 DTS 不够。

加了 `console=tty0` 之后：dummy 控制台从 0s 就启用，DRM fbdev 一注册（约 7s）fbcon 接管，企鹅 logo + 内核日志上屏。UART 仍然有一份拷贝。

---

## 1. 心得（给下一次「有图但启动黑屏」）

### 1.1 出图 ≠ 启动可见

P3 验收的是「用户态能看见品红 / framebuffer」。那只证明 **DPU→DSI→面板** 通。启动黑屏可以完全是软件层：

```
UEFI logo
  → 内核接管 MDSS（bootloader 画面被清掉）
  → DRM/DSI probe（几秒，屏物理上可能亮、内容全黑）
  → fbdev 注册，但 blank=4 / 无 tty0
  → systemd display-unblank.service（已经是用户态了）
```

用户要的「跑 Linux 内核的那一段也显示」，对应的是 **fbcon 成为 printk 控制台**，不是再画一张测试图。

### 1.2 配置开了 fbcon 不等于屏幕有 console

下面这些在 `config/ginkgo.fragment` 里早就有：

```
CONFIG_VT=y
CONFIG_VT_CONSOLE=y
CONFIG_FRAMEBUFFER_CONSOLE=y
CONFIG_FRAMEBUFFER_CONSOLE_DETECT_PRIMARY=y
CONFIG_LOGO=y
CONFIG_LOGO_LINUX_CLUT224=y
```

没有 `console=tty0` 时实机是：

| 项 | 值 | 含义 |
|----|----|------|
| `/proc/consoles` | 只有 `ttyMSM0`、`qcom_geni0` | printk 不去屏幕 |
| `vtcon1` bind | 1（frame buffer device） | fbcon 绑了 VT，但不是 console |
| `fb0/blank` | 启动后常为 4，直到 unblank 服务 | deferred fbdev 无人打开 |

加了 `console=tty0` 之后：

| 项 | 值 |
|----|----|
| `/proc/consoles` | `tty0 -WU (EC)` 为主控制台，UART 仍在 |
| dmesg | `printk: legacy console [tty0] enabled`（~0.01s，dummy） |
| dmesg | `Console: switching to colour frame buffer device 135x146`（DRM 起来后） |

`135x146` = 1080×2340 ÷ 8×16 字体。这就是 fbcon 几何，不是分辨率错了。

### 1.3 boot.img cmdline 才是真 cmdline

ABL 用 Android boot image 的 `--cmdline`。`chosen.bootargs` 只是备份，两边都改以免下次有人只看 DTS。

**必须改：** `scripts/build-bootimg.sh` 的 `CMDLINE`  
**一并改：** `linux/arch/arm64/boot/dts/qcom/sm6125-xiaomi-ginkgo.dts` 的 `chosen.bootargs`

`console=` 可以写多次。**最后一个** 成为 `/dev/console`（`CON_CONSDEV`）。当前顺序：

```
console=ttyMSM0,115200n8 console=tty0 ...
```

`tty0` 在后 → 屏幕是主控制台；`ttyMSM0` 仍注册，串口继续收 printk。

### 1.4 不要用 simplefb / cont_splash 冒充「内核阶段出图」

P3 已经明确：**产品路径是 DRM/DSI，不回退 simplefb。**  
`cont_splash` 能把 UEFI 画面多留一会儿，但内核一重置 MDP 还是会清掉。用户要的是 **内核自己的日志**，fbcon 才是对的层。

也不要在 `msm_fbdev` probe 里调 `drm_fb_helper_restore_fbdev_mode()`——ginkgo 实测卡死。fbcon 接管 + 已有的 `display-unblank.service` 足够。

---

## 2. 改之前的状态（2026-08-18，P3 已通）

| 时刻 | 发生了什么 | 用户看见 |
|------|------------|----------|
| 0s | UEFI logo | logo 闪一下 |
| 立刻 | 内核接管时钟 / 之后 MDSS reset | **黑屏** |
| ~2.5s | eMMC `mmcblk0` 起来 | 仍黑 |
| ~3.6s（加 tty0 前） | `panel init complete` | 仍黑（或仅背光） |
| ~4.2s | `Console: switching to colour frame buffer device`（无 tty0 时也会切 VT） | 仍几乎无字 |
| ~4.5s | `fb0: msmdrmfb` | deferred fbdev，常 `blank=4` |
| systemd 之后 | `display-unblank.service` → `echo 0 > fb0/blank` | 才稳定出图 |

`msm_kms.c` 里 `drm_client_setup` 被 **推迟 500ms** 到 workqueue：同步放在 mdss probe 里会拿着 `console_lock` 做第一次 atomic modeset，UART 看起来卡在 `no GPU device` 后面。这个延迟要留着，和 `tty0` 不冲突。

---

## 3. 实际改动

只动 cmdline 与注释，**不改** DPU/DSI/面板驱动。

### 3.1 `scripts/build-bootimg.sh`（生效点）

```
console=ttyMSM0,115200n8 console=tty0 earlycon=qcom_geni,0x4a90000 keep_bootcon ...
```

`DEBUG_BOOT=1` 那条同样加 `console=tty0`。

旧注释「no console=tty0 — fbcon races with async sdhci」删掉，换成：eMMC 在 DRM 之前 probe、`sdhc_2` 已禁用。

### 3.2 DTS `chosen.bootargs`

`linux/arch/arm64/boot/dts/qcom/sm6125-xiaomi-ginkgo.dts` 同步加上 `console=tty0`。  
`stdout-path` 仍是 `serial0:115200n8`（earlycon / 无 fb 时的后备）。

### 3.3 `config/ginkgo.fragment`

fbcon 相关 Kconfig 不变，只注明：**屏幕要出内核日志，cmdline 必须有 `tty0`。**

---

## 4. 验收（2026-08-18 `fastboot boot`）

第一次 `fastboot boot` 仍可能 `Sending` 失败（`Cannot send after transport endpoint shutdown`）。**不要 usbreset**（会弄丢 USB）。`killall -9 fastboot` 后重试即可。

### 4.1 控制台

```
/proc/consoles:
tty0                 -WU (EC     )    4:1
ttyMSM0              -W- (E   p  )  239:0
qcom_geni0           -W- (E B p  )

vtcon0  dummy device          bind=0
vtcon1  frame buffer device   bind=1

fb0/blank = 0
card0-DSI-1/dpms = On
```

`tty0` 的 `C` = preferred console。

### 4.2 dmesg 时间戳（本机这一次）

```
[    0.009] Console: colour dummy device 80x25
[    0.014] printk: legacy console [tty0] enabled
[    6.640] panel-tianma-nt36672a: panel init complete
[    7.200] Console: switching to colour frame buffer device 135x146
[    7.514] msm_dpu: [drm] fb0: msmdrmfb frame buffer device
```

加 `tty0` 之后 DRM 接管比「只开 UART」那次略晚（约 7s vs 4s）：`ignore_loglevel` + 1080×2340 **软件 blit** 会占 CPU。能看见日志比那几秒更重要。

### 4.3 屏幕上实际顺序（用户确认成功）

1. UEFI logo 闪一下  
2. 内核接管显示硬件，大约几秒黑屏（DRM 还没 probe，没法画）  
3. ~7s：企鹅 logo，随后内核日志在屏幕上滚动  
4. 用户态起来后仍是 framebuffer 控制台（GNOME 已关掉，不要恢复）

---

## 5. 不要做

| 不要 | 原因 |
|------|------|
| 回退 simplefb / 恢复 `cont_splash_mem` 当产品路径 | P3 已否；清 MDP 后 splash 保不住，也不是内核日志 |
| 在 `msm_fbdev` probe 里 `drm_fb_helper_restore_fbdev_mode()` | ginkgo 实测卡死；用 fbcon + `display-unblank.service` |
| 去掉 500ms `drm_client_setup` 延迟、改回 probe 同步 modeset | UART 会看起来卡死 |
| 只改 DTS `bootargs`、不改 `build-bootimg.sh` | ABL 用 boot.img cmdline |
| `fastboot flash boot` | 本仓库验证约定是 `fastboot boot` |
| 为出字去改 pinmux / 预取 / `CONFIG_FB` / 触控 | 显示链路已经通 |
| 把 `ignore_loglevel` 当成屏幕性能开关乱关 | UART 调试还靠它；真要加速再单独谈字体 / loglevel |

---

## 6. 复现 / 回归

```bash
# 打包（改过 DTS 时先 make dtbs）
./scripts/build-bootimg.sh

# 进 fastboot，不要 flash
./scripts/reboot-fastboot.sh
fastboot boot out/boot.img    # 第一次失败就 killall -9 fastboot 再试

# 起来后
./scripts/usb-connect.sh
./scripts/ssh-run.sh 'cat /proc/consoles; cat /sys/class/graphics/fb0/blank'
```

通过标准：`/proc/consoles` 有 `tty0` 且带 `C`；dmesg 有 `switching to colour frame buffer device`；**人眼**在 DRM 起来后能看到内核日志。

`display-unblank.service` 继续留着：SSH-only、没人打开 fb0 时 deferred fbdev 仍可能 `blank=4`。有 fbcon 当 console 时一般不需要它来「第一次点亮」，但它是安全网。

---

## 7. 和 P3 出图文档的边界

| 问题 | 文档 |
|------|------|
| 有背光无图像、FIFO `0xcccc`、INTF 预取 | [ginkgo-display-complete-2026-08-17.md](./ginkgo-display-complete-2026-08-17.md) |
| `dsi_err status=5` | [ginkgo-dsi-err-status5-analysis.md](./ginkgo-dsi-err-status5-analysis.md) |
| 用户态才亮、内核阶段黑、没有启动日志 | **本文** |

P3 修的是像素怎么从 DPU 送到面板。本文修的是 **printk 和 VT 怎么用已经存在的那块 fb**。
