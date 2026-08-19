**Language:** English | [简体中文](zh-CN/ginkgo-fbcon-boot-2026-08-18.md)

# Redmi Note 8 (ginkgo) mainline fbcon: from “black during kernel boot” to boot log on the panel

> Device: Xiaomi Redmi Note 8 · codename **ginkgo** · SoC **SM6125 (trinket)** · serial `<serial>`  
> Panel: Tianma **NT36672A** · 1080×2340@60 · mainline DRM / DPU / DSI (**do not fall back to simplefb**)  
> Acceptance date: evening 2026-08-18 · user confirmed they can see **Linux kernel logs** scrolling on the panel at boot  
> Prerequisite: same-device display P3 (2026-08-17 magenta scanout) already works. This doc only solves “kernel is running but the panel is black”.

**Related docs / skills**

| Doc | Contents |
|-----|----------|
| [ginkgo-display-complete-2026-08-17.md](./ginkgo-display-complete-2026-08-17.md) | DPU→DSI scanout (prerequisite here; link, FIFO, prefetch) |
| [ginkgo-wifi-complete-2026-08-18.md](./ginkgo-wifi-complete-2026-08-18.md) | Same-day WCN3990 associate (parallel work, unrelated to fbcon) |
| [ginkgo-display-bringup-methodology.md](./ginkgo-display-bringup-methodology.md) | DPMS / `fb0/blank` / `display-unblank.service` |
| [ginkgo-mainline-bringup-chronicle.md](./ginkgo-mainline-bringup-chronicle.md) | Whole-device bring-up timeline |
| [display-bringup-loop.sh](../scripts/display-bringup-loop.sh) | Black-screen layered SOP |
| [usb-connect.sh](../scripts/usb-connect.sh) | Reconfigure RNDIS after every reboot |
| [reboot-fastboot.sh](../scripts/reboot-fastboot.sh) | Enter fastboot from Ubuntu |

Verify with `fastboot boot out/boot.img`; **do not** `fastboot flash boot`.

---

## 0. One-sentence conclusion

Text during kernel boot appeared not because we “fixed DSI another round” or “ran systemd unblank earlier”, but because:

1. **Display hardware was already up at P3.** Panel `panel init complete` around 4–7 s, DRM immediately registers `fb0`. What was black was not the link — **nobody was writing kernel logs to this fb, and nobody in the kernel was turning DPMS on**.  
2. **`CONFIG_FRAMEBUFFER_CONSOLE` was already built in**; fbcon even binds the VT. But cmdline had **no** `console=tty0`, so printk only went to `ttyMSM0` and the panel had no console.  
3. `tty0` was deliberately omitted before; the comment said **fbcon raced with async SDHCI**. That was the simplefb era (fb registered late, SD slot still probing). Now eMMC is up by ~2.5 s, `sdhc_2` is disabled, DRM takes over around ~7 s — that old ban is obsolete.  
4. The cmdline that actually takes effect is in **`scripts/build-bootimg.sh`**. The Android boot.img overwrites DT `chosen.bootargs`; changing DTS alone is not enough.

After adding `console=tty0`: the dummy console enables from 0 s; as soon as DRM fbdev registers (~7 s) fbcon takes over; Tux logo + kernel log on the panel. UART still gets a copy.

---

## 1. Lessons (for the next “image works, boot is black”)

### 1.1 Pixels on screen ≠ visible at boot

P3 accepted “userspace can see magenta / framebuffer”. That only proves **DPU→DSI→panel** works. A black boot can be entirely software:

```
UEFI logo
  → kernel takes over MDSS (bootloader picture is cleared)
  → DRM/DSI probe (a few seconds; panel may be physically lit, content all black)
  → fbdev registers, but blank=4 / no tty0
  → systemd display-unblank.service (already userspace)
```

What the user wants — “the Linux kernel stretch also displays” — is **fbcon becoming the printk console**, not another test pattern.

### 1.2 Having fbcon in the config does not mean the panel has a console

These were already in `config/ginkgo.fragment`:

```
CONFIG_VT=y
CONFIG_VT_CONSOLE=y
CONFIG_FRAMEBUFFER_CONSOLE=y
CONFIG_FRAMEBUFFER_CONSOLE_DETECT_PRIMARY=y
CONFIG_LOGO=y
CONFIG_LOGO_LINUX_CLUT224=y
```

On device without `console=tty0`:

| Item | Value | Meaning |
|------|-------|---------|
| `/proc/consoles` | only `ttyMSM0`, `qcom_geni0` | printk does not go to the panel |
| `vtcon1` bind | 1 (frame buffer device) | fbcon bound the VT, but is not the console |
| `fb0/blank` | often 4 after boot until the unblank service | deferred fbdev, nobody opened it |

After adding `console=tty0`:

| Item | Value |
|------|-------|
| `/proc/consoles` | `tty0 -WU (EC)` is the primary console; UART still present |
| dmesg | `printk: legacy console [tty0] enabled` (~0.01s, dummy) |
| dmesg | `Console: switching to colour frame buffer device 135x146` (after DRM is up) |

`135x146` = 1080×2340 ÷ 8×16 font. That is fbcon geometry, not a wrong resolution.

### 1.3 boot.img cmdline is the real cmdline

ABL uses the Android boot image `--cmdline`. `chosen.bootargs` is only a backup; change both so the next person does not look only at DTS.

**Must change:** `CMDLINE` in `scripts/build-bootimg.sh`  
**Also change:** `chosen.bootargs` in `linux/arch/arm64/boot/dts/qcom/sm6125-xiaomi-ginkgo.dts`

`console=` can appear more than once. The **last** one becomes `/dev/console` (`CON_CONSDEV`). Current order:

```
console=ttyMSM0,115200n8 console=tty0 ...
```

`tty0` last → panel is the primary console; `ttyMSM0` is still registered, serial still receives printk.

### 1.4 Do not fake “kernel-stage image” with simplefb / cont_splash

P3 already stated: **the product path is DRM/DSI; do not fall back to simplefb.**  
`cont_splash` can keep the UEFI picture a bit longer, but once the kernel resets MDP it is gone. The user wants **the kernel’s own log**; fbcon is the right layer.

Also do not call `drm_fb_helper_restore_fbdev_mode()` in `msm_fbdev` probe — it hung on ginkgo. fbcon takeover + the existing `display-unblank.service` is enough.

---

## 2. State before the change (2026-08-18, P3 already up)

| Time | What happened | What the user saw |
|------|---------------|-------------------|
| 0s | UEFI logo | logo flashes |
| immediately | kernel takes clocks / later MDSS reset | **black screen** |
| ~2.5s | eMMC `mmcblk0` up | still black |
| ~3.6s (before tty0) | `panel init complete` | still black (or backlight only) |
| ~4.2s | `Console: switching to colour frame buffer device` (also switches VT without tty0) | still almost no text |
| ~4.5s | `fb0: msmdrmfb` | deferred fbdev, often `blank=4` |
| after systemd | `display-unblank.service` → `echo 0 > fb0/blank` | image finally stable |

In `msm_kms.c`, `drm_client_setup` is **deferred 500ms** onto a workqueue: doing it synchronously in mdss probe holds `console_lock` for the first atomic modeset, and UART looks stuck after `no GPU device`. Keep that delay; it does not conflict with `tty0`.

---

## 3. Actual changes

Cmdline and comments only; **do not** change DPU/DSI/panel drivers.

### 3.1 `scripts/build-bootimg.sh` (the one that takes effect)

```
console=ttyMSM0,115200n8 console=tty0 earlycon=qcom_geni,0x4a90000 keep_bootcon ...
```

The `DEBUG_BOOT=1` line also got `console=tty0`.

Old comment “no console=tty0 — fbcon races with async sdhci” deleted and replaced with: eMMC probes before DRM, `sdhc_2` is already disabled.

### 3.2 DTS `chosen.bootargs`

`linux/arch/arm64/boot/dts/qcom/sm6125-xiaomi-ginkgo.dts` also got `console=tty0`.  
`stdout-path` is still `serial0:115200n8` (fallback for earlycon / no fb).

### 3.3 `config/ginkgo.fragment`

fbcon-related Kconfig unchanged; only a note: **for kernel logs on the panel, cmdline must have `tty0`.**

---

## 4. Acceptance (2026-08-18 `fastboot boot`)

The first `fastboot boot` may still fail at `Sending` (`Cannot send after transport endpoint shutdown`). **Do not usbreset** (that loses USB). `killall -9 fastboot` and retry.

### 4.1 Consoles

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

`C` on `tty0` = preferred console.

### 4.2 dmesg timestamps (this device, this boot)

```
[    0.009] Console: colour dummy device 80x25
[    0.014] printk: legacy console [tty0] enabled
[    6.640] panel-tianma-nt36672a: panel init complete
[    7.200] Console: switching to colour frame buffer device 135x146
[    7.514] msm_dpu: [drm] fb0: msmdrmfb frame buffer device
```

After adding `tty0`, DRM takeover is a bit later than the UART-only boot (~7s vs 4s): `ignore_loglevel` + 1080×2340 **software blit** costs CPU. Seeing the log matters more than those seconds.

### 4.3 Actual on-panel order (user confirmed success)

1. UEFI logo flashes  
2. Kernel takes over display hardware; a few seconds of black (DRM not probed yet, cannot draw)  
3. ~7s: Tux logo, then kernel log scrolls on the panel  
4. After userspace is up it is still the framebuffer console (GNOME is already off; do not restore it)

---

## 5. Do not

| Do not | Why |
|--------|-----|
| Fall back to simplefb / restore `cont_splash_mem` as the product path | P3 already rejected this; splash cannot survive MDP reset, and it is not the kernel log |
| `drm_fb_helper_restore_fbdev_mode()` in `msm_fbdev` probe | hung on ginkgo; use fbcon + `display-unblank.service` |
| Drop the 500ms `drm_client_setup` delay and do modeset synchronously in probe | UART looks hung |
| Change only DTS `bootargs`, not `build-bootimg.sh` | ABL uses boot.img cmdline |
| `fastboot flash boot` | this repo’s verify convention is `fastboot boot` |
| Change pinmux / prefetch / `CONFIG_FB` / touch just to get text | the display link is already up |
| Treat `ignore_loglevel` as a panel-performance switch and randomly turn it off | UART debug still depends on it; if you really need speed, discuss font / loglevel separately |

---

## 6. Reproduce / regression

```bash
# pack (make dtbs first if DTS changed)
./scripts/build-bootimg.sh

# enter fastboot, do not flash
./scripts/reboot-fastboot.sh
fastboot boot out/boot.img    # if first try fails, killall -9 fastboot and retry

# after it is up
./scripts/usb-connect.sh
./scripts/ssh-run.sh 'cat /proc/consoles; cat /sys/class/graphics/fb0/blank'
```

Pass criteria: `/proc/consoles` has `tty0` with `C`; dmesg has `switching to colour frame buffer device`; **human eyes** can see the kernel log after DRM is up.

Keep `display-unblank.service`: on SSH-only boots with nobody opening fb0, deferred fbdev can still be `blank=4`. When fbcon is the console you generally do not need it for the “first light”, but it is a safety net.

---

## 7. Boundary vs the P3 scanout doc

| Problem | Doc |
|---------|-----|
| Backlight no image, FIFO `0xcccc`, INTF prefetch | [ginkgo-display-complete-2026-08-17.md](./ginkgo-display-complete-2026-08-17.md) |
| `dsi_err status=5` | [ginkgo-dsi-err-status5-analysis.md](./ginkgo-dsi-err-status5-analysis.md) |
| Only lights in userspace, black during kernel, no boot log | **this doc** |

P3 fixed how pixels get from DPU to the panel. This doc fixed **how printk and the VT use the fb that already exists**.
