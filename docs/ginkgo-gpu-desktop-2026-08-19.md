**Language:** English | [简体中文](zh-CN/ginkgo-gpu-desktop-2026-08-19.md)

# Redmi Note 8 (ginkgo) mainline Adreno 610: Ubuntu desktop from black screen to visible

> Device: Xiaomi Redmi Note 8 · codename **ginkgo** · SoC **SM6125 (trinket)** · serial `<serial>`  
> GPU: Qualcomm **Adreno 610** · chipid **`0x06010000`** · no standalone GMU firmware (`a6xx_gmuwrapper`)  
> System: mainline Linux 7.0 + Ubuntu 26.04 LTS (resolute) arm64, rootfs on userdata  
> Acceptance: 2026-08-19 01:53 · **user confirmed they reached the GNOME desktop**; overall a bit sluggish  
> Prerequisites: display P3, fbcon, touch P4, WiFi P5, and Ubuntu desktop software (session up but black screen) were already working.

**Related docs / skills**

| Document | Content |
|----------|---------|
| [ginkgo-ubuntu-desktop-2026-08-19.md](./ginkgo-ubuntu-desktop-2026-08-19.md) | TUNA mirrors + full GNOME installed; black screen at the time, no GPU |
| [ginkgo-display-complete-2026-08-17.md](./ginkgo-display-complete-2026-08-17.md) | DPU→DSI pixel path (do not regress) |
| [ginkgo-mainline-bringup-chronicle.md](./ginkgo-mainline-bringup-chronicle.md) | Full-device bring-up timeline |
| [firmware/ginkgo/README.md](../firmware/ginkgo/README.md) | zap / SQE sources |
| [usb-connect.sh](../scripts/usb-connect.sh) | Reconfigure RNDIS after every reboot |
| [reboot-fastboot.sh](../scripts/reboot-fastboot.sh) | Enter fastboot from Ubuntu |

Still verify with `fastboot boot out/boot.img`. **Do not** `fastboot flash boot`.

---

## 0. One-sentence conclusion

The desktop went from black to visible not because GNOME was reinstalled or DSI was changed, but because **mainline `msm` actually bound Adreno 610**, so Mesa can do 3D composition on the same DRM device.

What landed:

1. SM6125 DTS gained `gpu@5900000` + `gmu-wrapper` + `gpucc` + `adreno_smmu` (template is SM6115; the clock controller uses this SoC’s `qcom,sm6125-gpucc`).  
2. **Zap is device-signed** (vendor `a610_zap.mdt` + `.b00/.b01/.b02`); **SQE is from linux-firmware** (on-device `a630_sqe.fw` is version `0x187`; mainline requires `>= 0x190`).  
3. Xiaomi moved the GPU PIL reserved memory to **`0x57515000`**, not the SoC default `0x57115000`.  
4. `highest_bank_bit` is aligned with DPU/UBWC_CFG at **14** (13 first, then 15, both make GPU frames unreadable to the DPU — still looks like a black screen).  
5. `gdm` starts at boot as `display-manager.service`.

User-visible: **the GNOME desktop is on screen**. For later stutter and Resources missing temperature/GPU graphs, see [ginkgo-desktop-perf-resources-2026-08-19.md](./ginkgo-desktop-perf-resources-2026-08-19.md) (OSM CPUFreq + TSENS + `gpu_busy_percent`).

---

## 1. Which layer was black at the time

The desktop-software document already located this: GNOME succeeded at the process layer, and DSI/DPU had not regressed. What was broken is **3D / composition**:

```
/dev/dri/card0  →  msm_dpu (display-controller@5e01000)
Mesa            →  msm_dri.so
log             →  egl: failed to create dri2 screen
gnome-shell     →  ~one core at 100%, first frame never visible
```

There was no `gpu@` in the DTS and no Adreno/zap in the rootfs. `renderD128` was also attached to the DPU at that time.

Mainline `msm`’s model is: **KMS (DPU) and GPU (Adreno) bind on the same DRM card**. There is no extra `card1`. After GPU probe succeeds it is still `card0`; that card now has 3D.

Do not treat that black screen as a P3 display regression, and do not treat `kms_swrast` as the final solution — the user wants the native GPU.

---

## 2. Hardware and mainline driver facts

| Item | Value |
|------|-------|
| GPU | Adreno 610, `qcom,adreno-610.0` |
| chipid | `0x06010000` (downstream `trinket-gpu.dtsi`) |
| Registers | `0x5900000` / `cx_mem@0x599e000` / `cx_dbgc@0x5961000` |
| IRQ | GIC SPI **177** |
| SMMU | `0x59a0000`, `qcom,adreno-smmu` |
| GMU | **wrapper** (`gmu@596a000`), no standalone `a630_gmu.bin` |
| GPUCC | Kernel already has `gpucc-sm6125.c`, compatible `qcom,sm6125-gpucc`; fragment enables `CONFIG_SM_GPUCC_6125=y` |
| Frequencies | Downstream single SKU: 320 / 465 / 600 / 745 / 820 / 900 / 950 MHz; trinket **has no speedbin** |
| Zap PAS id | **13** (matches `a6xx_gpu.c` `GPU_PAS_ID`) |

Mainline `a6xx_catalog.c` explicitly lists A610 covering SM6125 (trinket), SM6115 (bengal), and SM6225 (khaje). Trinket has a single SKU; OPPs must not use `nvmem` / `opp-supported-hw`.

---

## 3. What was done (by pitfall)

### 3.1 DTS: copy the SM6115 GPU block onto SM6125

`sm6125.dtsi` originally had only the 8 KB `gpu_mem@57115000`, and no gpu/gmu/gpucc/adreno_smmu.

Written by comparing `sm6115.dtsi`:

- `gpu@5900000`: `qcom,adreno-610.0`, clocks `core/iface/mem_iface/alt_mem_iface/gmu/xo`, `qcom,gmu = <&gmu_wrapper>`, `status = "disabled"`  
- `gmu@596a000`: `qcom,adreno-gmu-wrapper`, CX/GX GDSC  
- `clock-controller@5990000`: `qcom,sm6125-gpucc` (**not** `sm6115-gpucc`); parent clocks are only XO + `GCC_GPU_GPLL0_CLK_SRC` (yaml has two; no DIV)  
- `iommu@59a0000`: IRQ list matches SM6115 (163, 167–174)

Board `sm6125-xiaomi-ginkgo.dts`: `&gpu { status = "okay"; }`, zap `firmware-name`.

OPPs hook the existing `rpmpd_opp_*`. SM6125 has no `turbo_plus`; 950 MHz uses `turbo`.

### 3.2 Zap address must be Xiaomi’s `0x57515000`

Downstream `ginkgo-trinket-memory.dtsi`, after growing ADSP, moved IPA/GPU PIL from `0x571xxxxx` to `0x575xxxxx`:

```
pil_gpu_mem: 0x57515000 / 8 KB
```

Zap in this device’s vendor partition is signed for that region. The mainline SoC dtsi still writes `0x57115000`. Board-level override:

```dts
&gpu_mem {
	reg = <0x0 0x57515000 0x0 0x2000>;
};
```

The ELF has a single `PT_LOAD`; `qcom_mdt_get_size` ≈ 4 KB, so 8 KB is enough.

### 3.3 Firmware: on-device zap, upstream SQE

Taken from the still-present **vendor partition** (`mmcblk0p85`):

| File | Use |
|------|-----|
| `a610_zap.mdt` + `.b00/.b01/.b02` | TZ PAS load; path `qcom/sm6125/xiaomi/ginkgo/` |
| `a610_zap.elf` | Reference only; not installed |
| `a630_sqe.fw` (vendor, 31980 B) | **Do not use**; payload version `0x187` |
| `a630_sqe.fw` (linux-firmware, 34188 B) | **Use this**; version `0x207` ≥ `0x190` |

`a6xx_ucode_check_version()` requires `a630_sqe.fw` `>= 0x190` or patched nibble `0xa`. The on-device SQE fails that check; the kernel returns `-EPERM`. The GPU looks probed but 3D never comes up.

SQE is not TZ-signed, so the upstream file can be substituted. Zap **must** be on-device.

Install paths:

- Repo: `firmware/ginkgo/gpu/`  
- initramfs: `/lib/firmware/qcom/...` (so the kernel can `request_firmware_direct` while still on the initramfs root)  
- Overlay: same path, lands on userdata  
- `scripts/configure-rootfs.sh` / `scripts/build-initramfs.sh` copy them  

### 3.4 `gmu` having no platform driver of its own is normal

`596a000.gmu` is **unbound** in sysfs. The GMU wrapper is initialized from the Adreno probe via the `qcom,gmu` phandle; it is not a standalone driver. `gpucc ... sync_state() pending due to 596a000.gmu` can be ignored.

Success signs:

```
msm_dpu ... bound 5900000.gpu (ops a3xx_ops)
loaded qcom/a630_sqe.fw from new location
/sys/kernel/debug/dri/0/gpu → gpu-initialized: 1, revision: 610 (6.1.0.0)
```

Firmware requests go through this DPU DRM device’s name. The log says `msm_dpu ... adreno_request_fw`; that is not the wrong device.

### 3.5 UBWC `highest_bank_bit`: 13 / 15 both keep the desktop black

| Attempt | GPU-side HBB | UBWC_CFG / DPU | Symptom |
|---------|--------------|----------------|---------|
| Mainline a610 default hardcoded | 13 | 14 | `Inconclusive ... 13 vs 14`; GPU is submitting; composed frames do not match the DPU |
| Only drop 13, take generic a6xx | 15 | 14 | `Inconclusive ... 15 vs 14`; same mismatch |
| Follow SoC `qcom_ubwc_cfg` | **14** | **14** | Warning gone; user sees the desktop |

Downstream `trinket-gpu.dtsi` is also `qcom,highest-bank-bit = <14>`. `sm6125_data.highest_bank_bit = 14`.

Change is in `a6xx_calc_ubwc_config()`: A610 uses `common_cfg->highest_bank_bit`; swizzle stays `0x7`.

### 3.6 Installing gdm does not mean it starts at boot

When `ubuntu-desktop` is installed after `graphical.target` has already been reached, gdm may be `disabled` with no `display-manager.service`. This boot autostart is:

```
/etc/systemd/system/display-manager.service → gdm.service
```

The overlay has the same symlink, so the next `fastboot boot` will lay it down again.

---

## 4. Acceptance (2026-08-19 01:53)

User confirmed: **already on the Ubuntu desktop.**

On-device cross-check:

```
gpu-initialized: 1
revision: 610 (6.1.0.0)
dmesg: bound 5900000.gpu; loaded qcom/a630_sqe.fw
dmesg: **no** Inconclusive highest_bank_bit
gdm.service: enabled + active
gnome-shell --mode=ubuntu
log: Created gbm renderer for '/dev/dri/card0'
log: GNOME Shell started
**no** egl: failed to create dri2 screen
fb0/blank=0, card0-DSI-1 dpms=On
GPU fences advancing (submit and retire)
```

Mesa 26: `libgallium-26.0.3` + `libEGL_mesa`; gnome-shell maps `/dev/dri/card0`. Still a single `card0` (DPU+GPU). That is the normal mainline msm shape.

### 4.1 “A bit sluggish”

This is a known current boundary, not “the GPU is not working”:

| Factor | Notes |
|--------|-------|
| GPU | Adreno 610, peak 950 MHz, phone SoC |
| Panel | 1080×2340, about 2.1× the pixels of 1080p |
| Compositor | GNOME + Wayland + `GSK_RENDERER=ngl` |
| CPU | Kryo 260 (4×A73 + 4×A53) |
| RAM | ~5.5 GiB, no swap; gnome-shell RSS has been ~380 MB |
| First desktop entry | Mesa shader cache has to compile once; CPU spikes, then it improves |

Fences submitting and retiring means the 3D path is running. The stutter is **610 pushing a full GNOME desktop**, not the software-render case of one core pegged with no frames.

If it needs to feel smoother later (separate task; do not touch display prefetch / touch / WiFi first):

- Confirm whether gnome-shell CPU drops at idle after the shader cache is warm  
- Evaluate a lighter session (still native GPU, not swrast)  
- GPU OPP / interconnect bandwidth (`gfx-mem` was not wired this time)  
- Whether heat and frequency are stuck on a low OPP  

---

## 5. Matching repo changes

| Path | Purpose |
|------|---------|
| `linux/arch/arm64/boot/dts/qcom/sm6125.dtsi` | gpu / gmu-wrapper / gpucc / adreno_smmu + `gpu_mem` |
| `linux/arch/arm64/boot/dts/qcom/sm6125-xiaomi-ginkgo.dts` | enable GPU; zap path; PIL `0x57515000` |
| `linux/drivers/gpu/drm/msm/adreno/a6xx_gpu.c` | A610 `highest_bank_bit` follows SoC UBWC_CFG |
| `config/ginkgo.fragment` | `CONFIG_SM_GPUCC_6125=y` |
| `firmware/ginkgo/gpu/` | On-device zap + linux-firmware SQE |
| `scripts/build-initramfs.sh` | Pack GPU firmware into initramfs and overlay |
| `scripts/configure-rootfs.sh` | Install the same firmware on a full rootfs |
| `rootfs-overlay/etc/systemd/system/display-manager.service` | Start gdm at boot |

---

## 6. Do not

| Do not | Why |
|--------|-----|
| Change DSI prefetch / pinmux / `CONFIG_FB` to “feel smoother” | The pixel path is fine |
| Fall back to simplefb | The desktop uses DRM/KMS |
| Use `kms_swrast` / `LIBGL_ALWAYS_SOFTWARE` as a long-term solution | The user wants native GPU; that was a black-screen workaround |
| Swap in linux-firmware’s generic `a610_zap` | Device-signed; PAS will reject it |
| Use the `a630_sqe.fw` from the vendor partition | Version too old; mainline rejects it outright |
| Move zap PIL back to `0x57115000` | Xiaomi board-level is `0x57515000` |
| Hard-code a610 HBB to 13 again, or take generic 15 | Misaligned with DPU’s 14; composed picture is black |
| `fastboot flash boot` | This repo’s convention is `fastboot boot` only |
| Bounce `wlan0` / change the MAC | Breaks WLAN.HL.3.x |
| Treat `596a000.gmu` unbound as “GMU never came up” | The wrapper is not a standalone platform driver |
| Treat “only `card0`” as “GPU never registered” | msm is DPU+GPU on the same card |

The first `fastboot boot` may still fail Sending: **do not usbreset**. `killall -9 fastboot` and retry. Host NetworkManager will wipe `192.168.7.1` on `enx*`; SSH is more reliable if that interface is unmanaged first.

---

## 7. Boundaries vs other completion docs

| Topic | Document |
|-------|----------|
| Backlight but no image, FIFO, INTF prefetch | [Display image](./ginkgo-display-complete-2026-08-17.md) |
| TUNA mirrors, install full GNOME, gdm pitfalls, **black screen at the time** | [Ubuntu desktop software](./ginkgo-ubuntu-desktop-2026-08-19.md) |
| Adreno 610, desktop visible | **This document** |
| Stutter, Resources temperature/GPU graphs, BWMON hang | [Desktop performance](./ginkgo-desktop-perf-resources-2026-08-19.md) |
| Scan / associate | [WiFi](./ginkgo-wifi-complete-2026-08-18.md) |

---

## 8. Regression commands

```bash
# Is the GPU up in the kernel
dmesg | grep -iE 'bound 5900000.gpu|a630_sqe|Inconclusive highest_bank'
head -12 /sys/kernel/debug/dri/0/gpu

# Desktop
systemctl is-active gdm.service
pgrep -a gnome-shell
journalctl -b | grep -E 'dri2 screen|GNOME Shell started|Created gbm renderer'

# Display not turned off by the compositor
cat /sys/class/graphics/fb0/blank
cat /sys/class/drm/card0-DSI-1/dpms
```

Expect: `gpu-initialized: 1`, `revision: 610`, **no** `Inconclusive highest_bank_bit`, **no** `failed to create dri2 screen`, and `GNOME Shell started`.
