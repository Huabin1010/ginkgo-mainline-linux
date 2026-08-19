**Language:** English | [简体中文](zh-CN/ginkgo-ubuntu-desktop-2026-08-19.md)

# Redmi Note 8 (ginkgo) Ubuntu desktop: TUNA mirrors, full GNOME installed, session up, screen still black

> **Follow-up (later the same day):** native Adreno 610 came up; the user confirmed they reached the desktop. For the black-screen cause and GPU bring-up, see [ginkgo-gpu-desktop-2026-08-19.md](./ginkgo-gpu-desktop-2026-08-19.md). This document keeps the “software installed, screen was black at the time” process. Do not apply the section 3 swrast workaround to this machine as it is now.

> Device: Xiaomi Redmi Note 8 · codename **ginkgo** · SoC **SM6125 (trinket)** · serial `<serial>`  
> System: mainline Linux 7.0 + **Ubuntu 26.04 LTS (resolute)** arm64, rootfs on userdata (`mmcblk0p87`, ~49 G)  
> Acceptance: early morning 2026-08-19  
> Prerequisites: display P3, fbcon boot log, touch P4, and WiFi P5 were already working.

**Related docs / skills**

| Document | Content |
|----------|---------|
| [ginkgo-display-complete-2026-08-17.md](./ginkgo-display-complete-2026-08-17.md) | DPU→DSI image (pixel path) |
| [ginkgo-fbcon-boot-2026-08-18.md](./ginkgo-fbcon-boot-2026-08-18.md) | Kernel boot log on screen |
| [ginkgo-wifi-complete-2026-08-18.md](./ginkgo-wifi-complete-2026-08-18.md) | WCN3990 online (prerequisite for this apt run) |
| [ginkgo-mainline-bringup-chronicle.md](./ginkgo-mainline-bringup-chronicle.md) | Full-device bring-up timeline |
| [usb-connect.sh](../scripts/usb-connect.sh) | USB RNDIS SSH |
| [reboot-fastboot.sh](../scripts/reboot-fastboot.sh) | Enter fastboot from Ubuntu |

Verify the kernel with `fastboot boot out/boot.img`. **Do not** `fastboot flash boot`. The desktop is installed on the **userdata rootfs**; the boot partition is not written.

---

## 0. One-sentence conclusion (what landed + the boundary)

This work was not another DSI fix. It turned the minimal rootfs into an **auto-login Ubuntu GNOME desktop stack**. What landed:

1. **apt switched to Tsinghua TUNA `ubuntu-ports`** (including universe / updates / security). This device cannot reach `ports.ubuntu.com`.  
2. **The full `ubuntu-desktop` metapackage is installed** (~1277 new packages, 718 MB download / ~3 GB installed).  
3. **`gdm3` + `gnome-shell --mode=ubuntu` are running**; user `ginkgo` auto-logs in on `seat0` / `tty2`.  
4. **Home WiFi SSH works**: `ssh root@<wlan0-dhcp>` or `ssh ginkgo@<wlan0-dhcp>`, password `$GINKGO_ROOT_PASSWORD`. USB `192.168.7.2` still works.

What had not landed (the user saw a **black screen**):

- **No native Adreno 3D GPU.** `/dev/dri/card0` is the **DPU display controller**, not the GPU.  
- Mesa sees that DRM node and loads **`msm_dri`**. Log: `egl: failed to create dri2 screen`. mutter still created a GBM renderer.  
- `gnome-shell` saturates about **one CPU core (~102%)**, RSS ~480 MB. The session registers successfully, but **the compositor never delivers a visible frame to the panel**.  
- This is not “the desktop was not installed,” and it is not “DSI broke again” (`dpms=On`, `fb0/blank=0`, connector `enabled`).

The next step should be to **force Mesa onto `kms_swrast` / software GL, and if needed disable Wayland and use Xorg**. Do not reboot the whole machine first. Mainline Adreno 610 GPU is a separate bring-up.

---

## 1. What is on the machine now

| Item | Status |
|------|--------|
| Ubuntu | 26.04 LTS resolute, `LANG=zh_CN.UTF-8` |
| Disk | userdata ~49 G, still plenty of free space after the desktop install |
| RAM | 5.4 GiB, no swap |
| CPU | Kryo 260 8 cores; idle load ~1.3; after GNOME starts, gnome-shell ~100% (software render) |
| Display path | DPU/DSI still healthy; fbcon can still show the kernel log when GNOME is not running |
| Touch / WiFi | Still P4 / P5; drivers were not changed for the desktop |
| 3D GPU | **Not enabled** (no `gpu@` node, no Adreno/zap firmware) |
| Users | `ginkgo` (uid 1000), sudo, password `$GINKGO_ROOT_PASSWORD`; root also `$GINKGO_ROOT_PASSWORD` |
| Autologin | `/etc/gdm3/custom.conf`: `AutomaticLogin=ginkgo`, `WaylandEnable=true` |
| Default target | `graphical.target`; `gdm.service` enabled |

Software confirmation (after 2026-08-19 01:15):

```
ii  gdm3                   50.1-0ubuntu0.1
ii  ubuntu-desktop         1.570.2
ii  gnome-shell            50.1-0ubuntu1.2
gnome-shell --mode=ubuntu
gdm-wayland-session /usr/bin/gnome-session --session=ubuntu
```

---

## 2. What was done (in order)

### 2.1 TUNA mirrors

The device originally had only:

```
deb http://ports.ubuntu.com/ubuntu-ports resolute main
```

and `ports.ubuntu.com` did not respond to ping. TUNA HTTP was reachable (~45–70 ms).

Files that landed:

- Device + overlay: `rootfs-overlay/etc/apt/sources.list.d/ubuntu.sources`  
- Overlay: `rootfs-overlay/etc/apt/sources.list` (comments only, to avoid duplicating DEB822)  
- Future new rootfs: `scripts/build-rootfs.sh` `MIRROR` defaults to TUNA

Security also goes through TUNA: this device cannot reach the official security archive. TUNA’s docs advise against replacing security, but there is no other option here.

HTTPS failed on the first try: the phone RTC was stuck at **2026-04-15**, so Release/certificates were “not yet valid.” After setting the clock to 2026-08-18, HTTP `apt update` worked. There is no RTC, so the clock will drift again after reboot. `systemd-timesyncd` is already installed with the desktop and should NTP once the network is up.

### 2.2 Full `ubuntu-desktop`

Not `ubuntu-desktop-minimal`. Measured on TUNA:

- Download **726 MB / repo 853 MB** (some already in cache)  
- About **3.0 GB** after install  
- WiFi `<test-ap-5g>`, air rate 433 Mbit/s VHT80; apt averaged **~9.7 MB/s** (718 MB / 74 s), normal for a pile of small debs  

Log: `/tmp/ubuntu-desktop-install.log`. `INSTALL_EXIT:0` appeared at **2026-08-19 01:06**. Unpacking was relatively fast; **`dpkg --configure` was the slow part** (a thousand-plus packages running postinst serially: ldconfig, fonts, systemd), about 40–50 minutes.

Also installed for Chinese: `fonts-noto-cjk`, `ibus-libpinyin`, `locales`.

`btop` / `htop`: already queued while the desktop apt lock was held; they should install after the desktop finishes. Treat on-device `dpkg -l btop htop` as the source of truth.

### 2.3 Why the desktop still did not start after install

`apt` finished **after the system had already booted**. `graphical.target` had already been reached at boot, when gdm was not installed yet, so systemd **does not go back and start a display manager**. The screen stayed on fbcon.

Worse: `gdm3` / `ubuntu-desktop` were **`iU` (unpacked, not configured)** at that point.

| Missing | Symptom |
|---------|---------|
| System user `gdm` not created (`systemd-sysusers gdm3.conf` never ran) | `generate-config`: `install: invalid user: 'gdm'`; gdm would not start |
| `/etc/pam.d/gdm-autologin` and friends still sat as `*.dpkg-new` | Autologin PAM failed: `auth could not identify password for [ginkgo]` |
| Overlay already had `custom.conf` | dpkg asks about the conffile when configuring gdm3; must `--force-confold`, or an interactive EOF fails configure again |

Fix:

```
systemd-sysusers gdm3.conf          # create the gdm user
put gdm-*.dpkg-new in place
DEBIAN_FRONTEND=noninteractive dpkg --force-confold --configure gdm3
dpkg --configure ubuntu-desktop-minimal ubuntu-desktop
keep the overlay AutomaticLogin=ginkgo
systemctl start/restart gdm.service
```

After that: `session opened for user ginkgo`, `gnome-shell --mode=ubuntu`, `GNOME Shell started`.

### 2.4 WiFi SSH

Phone DHCP: `wlan0` **`<wlan0-dhcp>/24`**; the host wired NIC is on the same subnet. sshd listens on `0.0.0.0:22`, `PermitRootLogin yes`.

```bash
ssh root@<wlan0-dhcp>      # do not add -b 192.168.7.1
ssh ginkgo@<wlan0-dhcp>
# password: see overlay
```

The IP is DHCP and may change next time. If unsure, use USB SSH and check `ip -4 addr show wlan0`.

---

## 3. Black-screen root cause (identified; render path not changed yet)

GNOME **succeeded at the process layer**. What was black is the **3D / compositor path**:

```
gnome-shell logs:
  libEGL warning: egl: failed to create dri2 screen
  Failed to initialize accelerated iGPU/dGPU framebuffer sharing: Not hardware accelerated
  Created gbm renderer for '/dev/dri/card0'

ls /usr/lib/aarch64-linux-gnu/dri | grep -E 'msm|swrast'
  kms_swrast_dri.so
  msm_dri.so
  swrast_dri.so
```

`card0`’s driver is **`msm_dpu`** (`display-controller@5e01000`), not Adreno. Mesa still prefers `msm_dri`. There is no `gpu@` in the DTS, and the rootfs has no Adreno/zap firmware.

Current GNOME environment: `XDG_SESSION_TYPE=wayland`, `GSK_RENDERER=ngl` (overlay `99-ginkgo-gnome.conf`). ngl + failed msm EGL + a 1080×2340 software attempt → one core pegged, first frame never visible.

**Do not** treat this black screen as a P3 display regression. Layers:

| Layer | Now |
|-------|-----|
| L1–L3 panel/DSI/DPU | Working |
| L4 fbdev/DPMS | `blank=0`, `dpms=On` |
| L5 GNOME/Wayland/EGL | **Failed visible output** |

Suggested next cuts (not done yet):

1. `MESA_LOADER_DRIVER_OVERRIDE=kms_swrast` (or `LIBGL_ALWAYS_SOFTWARE=1`)  
2. `GSK_RENDERER=cairo` (software path is more stable than ngl)  
3. If still black, `WaylandEnable=false`, then Xorg + llvmpipe  
4. **`systemctl restart gdm` first; do not reboot the whole machine** (no RTC; USB SSH must be reconfigured)  
5. Adreno 610 mainline GPU: DT node + firmware, separate task

---

## 4. Matching repo changes

| Path | Purpose |
|------|---------|
| `rootfs-overlay/etc/apt/sources.list.d/ubuntu.sources` | TUNA DEB822 |
| `rootfs-overlay/etc/apt/sources.list` | Clear the old one-line source to avoid duplicates |
| `scripts/build-rootfs.sh` | New debootstrap defaults to TUNA |
| `rootfs-overlay/etc/gdm3/custom.conf` | ginkgo autologin, Wayland |
| `rootfs-overlay/etc/dconf/db/local.d/00-ginkgo-desktop` | On-screen keyboard, scale 2, no idle sleep |
| `rootfs-overlay/etc/environment.d/99-ginkgo-gnome.conf` | `GSK_RENDERER=ngl` (**should change after the black screen**) |
| `rootfs-overlay/usr/local/sbin/display-unblank.sh` | Unblank only; does not draw a test pattern (avoids stomping the desktop) |

The initramfs overlay is reapplied on every boot; `ubuntu.sources` and `custom.conf` come back with it.

---

## 5. Do not

| Do not | Why |
|--------|-----|
| Reboot first because of a black screen | Clock is lost; USB must be reconfigured; a render bug will still be black after reboot |
| Fall back to simplefb / change DSI prefetch to “save the desktop” | The pixel path is fine |
| Call `restore_fbdev_mode()` in `msm_fbdev` probe | Hard-locks on ginkgo in practice |
| `fastboot flash boot` | This repo’s convention is `fastboot boot` only |
| Bounce `wlan0` / change the MAC for the desktop | Breaks WLAN.HL.3.x |
| Treat `msm_dri` failure as “GNOME was not installed” | Packages and session are both there |

---

## 6. Boundaries vs other completion docs

| Topic | Document |
|-------|----------|
| Backlight but no image, FIFO, INTF prefetch | [Display image](./ginkgo-display-complete-2026-08-17.md) |
| No boot log at the kernel stage | [fbcon](./ginkgo-fbcon-boot-2026-08-18.md) |
| Scan / associate / rates | [WiFi](./ginkgo-wifi-complete-2026-08-18.md) |
| TUNA mirrors, full desktop, gdm, black screen at the time (no GPU) | **This document** (process) |
| Adreno 610, desktop visible, a bit sluggish | [GPU desktop](./ginkgo-gpu-desktop-2026-08-19.md) |

---

## 7. Regression commands

```bash
# WiFi (use the address on the device)
ssh ginkgo@<wlan0-dhcp>

# Is the session up
loginctl
pgrep -a gnome-shell
systemctl is-active gdm.service

# Is it using the broken msm EGL again
journalctl _UID=1000 --since "5 min ago" | grep -E "dri2 screen|llvmpipe|kms_swrast|GNOME Shell started"
```
