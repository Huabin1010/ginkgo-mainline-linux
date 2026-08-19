**Language:** English | [简体中文](zh-CN/ginkgo-touch-complete-2026-08-17.md)

# Redmi Note 8 (ginkgo) mainline touch: from red NO TOUCH DEVICE to on-screen dots

> Device: Xiaomi Redmi Note 8 · codename **ginkgo** · SoC **SM6125 (trinket)** · serial `<serial>`  
> Touch IC: **Novatek NT36672A** · bus **SPI** (not I2C) · 1080×2340  
> Controller: QUP v3 **SE2** = mainline `&spi2` = `spi@4a88000`  
> Panel: Tianma NT36672A (same IC family as the touch controller; display is DSI, touch is a separate SPI bus)  
> Stack: downstream `nt36xxx_spi_c3j` ported to mainline 7.0 + built-in `novatek_ts_tianma_fw.bin`  
> Acceptance: evening of 2026-08-17 · user confirmed “ok 非常的完美”  
> Prerequisite: same-day display P3 already showed an image; see [ginkgo-display-complete-2026-08-17.md](./ginkgo-display-complete-2026-08-17.md)

**Related docs / skills**

| Document | Content |
|----------|---------|
| [ginkgo-display-complete-2026-08-17.md](./ginkgo-display-complete-2026-08-17.md) | Same-day DPU→DSI image (prerequisite for touch) |
| [ginkgo-mainline-bringup-chronicle.md](./ginkgo-mainline-bringup-chronicle.md) | Full-device bring-up timeline |
| [firmware/ginkgo/README.md](../firmware/ginkgo/README.md) | Tianma firmware extracted from this unit |
| [reference/README.md](../reference/README.md) | Downstream driver / DTS reference |
| [ginkgo-touch-test.sh](../scripts/ginkgo-touch-test.sh) | Agent touch-regression SOP |
| [usb-connect.sh](../scripts/usb-connect.sh) | Reconfigure RNDIS after every reboot |
| [reboot-fastboot.sh](../scripts/reboot-fastboot.sh) | Enter fastboot from Ubuntu |

---

## 0. One-sentence conclusion

Touch finally produced points not because we “deferred probe a few more times” or “waited longer for panel bias”, but because of **three stacked facts**:

1. **Electrically, CS is active-low (`SPI_MODE_0`).** Downstream DTS writes `spi-cs-high`, but the downstream driver immediately does `spi->mode = SPI_MODE_0` and clears it. If mainline keeps the DT `SPI_CS_HIGH` (`mode=4`), MISO always reads all `0xFF`.  
2. **NT36672A is a host-download SPI chip:** chip ID only proves the bootloader is still alive; reporting points requires downloading `novatek_ts_tianma_fw.bin` into SRAM after boot.  
3. **`nt36xxx.c` `#undef CONFIG_FB`; the firmware file does not.** The two sides see different `struct nvt_ts_data` layouts. `fw_update` reads `ts->mmap` as NULL, and firmware download Oopses (`ESR 0x96000005`, `ldr w0,[x0]`). After aligning them: **firmware finished in 377 ms, PID=`591F`, user tapping the screen produced points.**

Along the way we also hit: sleep pinctrl switching MOSI/MISO/CLK/CS back to gpio (also all `0xFF`), GENI SPI walking the DMA path for a 63 KB single transfer when there is no GPI DMA, `fastboot boot` stuck at Sending, and after an Oops sshd dying so a human had to enter fastboot.

---

## 1. Lessons (for the next SPI touch / downstream driver port)

### 1.1 Mainline `novatek-nvt-ts` is useless for ginkgo

Mainline `drivers/input/touchscreen/novatek-nvt-ts.c` is **I2C-only**. ginkgo’s NT36672A sits on **SPI**, and the compatible is not `novatek,nvt-ts`. Do not hard-wire an I2C node in DTS “just to try”.

Correct sources:

```
reference/downstream/drivers/nt36xxx_spi_c3j/
reference/downstream/dts/xiaomi/ginkgo/ginkgo-trinket-touchscreen.dtsi
firmware/ginkgo/novatek_ts_tianma_fw.bin
```

### 1.2 Before reading the chip datasheet, read what the driver actually writes

Downstream DTS clearly has `spi-cs-high`. If you trust DT alone, you make CS active-high. On-device `dmesg`:

| Driver write | `mode=` | Meaning | Result |
|--------------|---------|---------|--------|
| Mainline once: `mode &= ~(SPI_CPOL\|SPI_CPHA)` keep DT | **4** = `SPI_CS_HIGH` | CS active-high | all `0xFF` |
| Downstream: `mode = SPI_MODE_0` | **0** | CPOL=0 CPHA=0, CS **active-low** | chip ID `0x0A 00 00 72 66 03` |

The `spi-cs-high` in DT is a dead config; the driver override is the electrical truth. When porting, **compare `mode` before and after `spi_setup` in probe**. Do not only diff the dtsi.

All `0xFF` almost always means: **CS not selected / clock not running / pins are not in SPI function**, MISO pulled up. Not “the chip is dead”, and not “firmware not loaded” (you have not reached firmware yet).

### 1.3 Qualcomm GENI SPI: sleep pinctrl and DMA are both traps

QUP SE `spi-geni-qcom` switches to `pinctrl-1` (sleep) on `runtime_suspend` / `resources_off`. If the sleep state is `gpio` instead of `qup02`, the next transfer has MOSI/MISO/CLK/CS all as GPIO, and reads are all `0xFF`. ginkgo’s fix: **both default and sleep point at `qup_spi2_active`**.

`geni_can_dma()`: if transfer length is **greater than FIFO** (on this device, roughly 64-byte class), it claims “can DMA”. Downstream relies on GPI DMA (`dmas`). Mainline ginkgo deleted `dmas`/`dma-names` (FIFO-only). A single `spi_sync` of 63×1024 bytes walks the DMA path; when the controller has no usable DMA channel this shows up as a hang or a later Oops.  
Current `NVT_TRANSFER_LEN = 32`, chopping firmware into 32-byte chunks over FIFO. This is the correct approach under the constraint, not “arbitrarily shrinking it”.

### 1.4 Multiple compilation units must see the same struct

The downstream driver compiles the FB notifier into `struct nvt_ts_data` (`#if CONFIG_FB`). Mainline has no `FB_EARLY_EVENT_BLANK`; `nt36xxx.c` `#undef CONFIG_FB` **before including the header**. `nt36xxx_fw_update.c` only `#include "nt36xxx.h"`, kernel `CONFIG_FB=y` (msmdrmfb), so:

```
nt36xxx.c     ts->mmap offset = A
fw_update.c   ts->mmap offset = A + sizeof(workqueue*) + work_struct + notifier_block
```

Which produced:

- Firmware filename printed empty (read the wrong `boot_update_firmware_name`) → `request_firmware` returned `-EINVAL` (-22)  
- `ts->hw_crc` read as 0, took the no-HW-CRC download path  
- `ts->mmap` read as NULL → `nvt_write_addr(ts->mmap->EVENT_BUF_ADDR | …)` → Oops

**Hard rule:** any `#undef` / `#define` that changes struct layout must go in the **header**, shared by all `.c` files. Do not undef in only one `.c`.

### 1.5 Chip ID working ≠ can report points

`nvt_ts_check_chip_ver_trim` only talks to the bootloader. NT36672A needs the host to write FW into SRAM, then `nvt_boot_ready()`. Probe succeeding, `/dev/input/event1` appearing, IRQ count still 0 — normal when nobody is touching the screen; if touching still yields 0, first check whether firmware logged `Update firmware success`.

### 1.6 Touch and panel share power; do not randomly shut it down on the failure path

| Rail | Touch DT | Panel DT | Note |
|------|----------|----------|------|
| L9A 1.8V | `touch_vddio` | `vddio` | IO, both need it |
| LCDB LDO (+) | `touch_lab` | `vddpos` | analog bias, **shared** |
| LCDB NCP (-) | `touch_ibb` | `vddneg` | same |

On probe failure, `regulator_disable(lab/ibb)` may turn LCDB off before the panel has enabled it. lab/ibb were changed to `regulator_get_optional`; do not leave LCDB off for long on the touch error path.

### 1.7 Debug channel: SSH may die after an Oops

A firmware-thread Oops does not necessarily panic the whole machine, but sshd / gadget may stop responding. Ping works, SSH refused = kernel still up, userspace half-dead. Then **do not sit waiting on connect.sh**; have the user key into fastboot and `fastboot boot out/boot.img` (**do not** `fastboot flash boot`).

After every successful `fastboot boot`:

```bash
./scripts/usb-connect.sh
```

`enx*` changes every time.

### 1.8 Change one variable at a time

The order that actually worked:

1. Pin mux to `qup02` (otherwise do not talk about protocol)  
2. CS polarity to MODE_0 (otherwise do not talk about chip ID)  
3. Struct alignment (otherwise do not talk about firmware)  
4. Transfer length ≤ FIFO (otherwise large downloads walk the broken DMA path)  

Stacking defer, wait-for-panel, CS change, and pinctrl change in the same build makes the log fail to tell you which cut actually took effect.

---

## 2. Goals, acceptance, non-goals

### 2.1 Goal

On mainline Linux, walk the full path **QUP SE2 SPI → NT36672A bootloader → host download Tianma FW → input subsystem**, visible when the user taps the screen.

### 2.2 Quantitative acceptance (measured 2026-08-17)

| Metric | Success threshold | Measured |
|--------|-------------------|----------|
| SPI `mode` | 0 (MODE_0) | `mode=0, max_speed_hz=8000000` |
| Chip ID | not all `0xFF`, hits trim table | `0x0A 00 00 72 66 03`, “This is NVT touch IC” |
| Firmware | `Update firmware success` | `<376682 us>`, no Oops |
| FW info | `nvt_get_fw_info` / PID | `FW type is 0x01`, `PID=591F` |
| Input node | `NVTCapacitiveTouchScreen` | `/dev/input/event1`, ABS 0..1079 × 0..2339 |
| Visualization | green **TOUCH OK**, tapping produces dots | user: “ok 非常的完美” |
| Oops | `dmesg \| grep Oops` is 0 | 0 after alignment |

### 2.3 Non-goals (not this time)

- Gesture wake / double-tap to wake productization  
- MP factory test (`NVT_TOUCH_MP=0`)  
- `/proc` debug nodes (`NVT_TOUCH_PROC=0`, `NVT_TOUCH_EXT_PROC=0`)  
- Focaltech other IC (downstream DTS is compatible; this unit is Tianma Novatek)  
- Baking `fastboot boot` into the boot partition (needs explicit user request)  
- WiFi

---

## 3. Hardware and pins (vs downstream)

### 3.1 Bus

Downstream node: `&qupv3_se2_spi` → mainline `&spi2` (`spi@4a88000`).

| Signal | TLMM | Function |
|--------|------|----------|
| MOSI | gpio6 | `qup02` |
| MISO | gpio7 | `qup02` |
| CLK  | gpio8 | `qup02` |
| CS   | gpio9 | `qup02` |
| RESET | gpio87 | gpio, output high |
| IRQ   | gpio88 | gpio, pull-up input, rising edge |

`spi-max-frequency = <8000000>` (8 MHz), same as downstream.

### 3.2 Why DMA was deleted

Mainline `sm6125.dtsi` spi2 has `dmas` / `dma-names` (GPI). On ginkgo, GENI GPI DMA was not validated as the touch path; to avoid probe hanging on DMA, DTS has:

```dts
/delete-property/ dmas;
/delete-property/ dma-names;
```

Paired with `NVT_TRANSFER_LEN=32` over FIFO.

### 3.3 pinctrl (the decisive detail)

```dts
qup_spi2_active: qup-spi2-active-state {
    pins = "gpio6", "gpio7", "gpio8", "gpio9";
    function = "qup02";
    drive-strength = <6>;
    bias-disable;
};

&spi2 {
    pinctrl-0 = <&qup_spi2_active>;
    pinctrl-1 = <&qup_spi2_active>;   /* sleep 也保持 qup02 */
    pinctrl-names = "default", "sleep";
    ...
};
```

Early on, when sleep switched back to `gpio`: gpio6–9 `function` became 0, MISO all `0xFF`. The comment is in the DTS; do not “casually” change it back to standard sleep-gpio.

### 3.4 Firmware

Extracted from this unit’s Android partitions; see `firmware/ginkgo/README.md`:

| File | Size | Use |
|------|------|-----|
| `novatek_ts_tianma_fw.bin` | 118784 B | **this unit’s Tianma, actually used** |
| `novatek_ts_tianma_mp.bin` | 118784 B | MP test, not enabled |
| `novatek_ts_ebbg_fw.bin` | 118784 B | EBBG panel variant, not used on this unit |

Kernel fragment:

```
CONFIG_SPI_QCOM_GENI=y
CONFIG_TOUCHSCREEN_NT36XXX_SPI=y
CONFIG_EXTRA_FIRMWARE="novatek_ts_tianma_fw.bin"
CONFIG_EXTRA_FIRMWARE_DIR="…/firmware/ginkgo"
```

On device `fw_need_write_size = 110592 (0x1b000)` (payload before the NVT end flag at the end of the bin).

The cmdline ABL passes down contains `dsi_nt36672a_tianma_vid_display`, so `CHECK_TOUCH_VENDOR` can `strstr(..., "tianma")` and hit. Do not rely on your own mkbootimg short cmdline also having tianma — if missing it falls through to default tianma, which happens to still be correct on this unit.

---

## 4. Software landing spots

| Item | Path |
|------|------|
| Driver | `linux/drivers/input/touchscreen/nt36xxx/` (`nt36xxx.c` / `nt36xxx_fw_update.c` / `nt36xxx.h` / `nt36xxx_mem_map.h`) |
| Kconfig | `TOUCHSCREEN_NT36XXX_SPI` |
| DTS | `&spi2` + pinctrl in `linux/arch/arm64/boot/dts/qcom/sm6125-xiaomi-ginkgo.dts` |
| Firmware | `firmware/ginkgo/novatek_ts_tianma_fw.bin` (built into vmlinux) |
| Visualization | `scripts/ginkgo-touch-test.py` → overlay `/usr/local/sbin/ginkgo-touch-test.py` |
| Downstream reference | `reference/downstream/drivers/nt36xxx_spi_c3j/` |

Mainline API port notes (vs downstream 4.14 style):

- `spi->master` → `spi->controller`  
- `remove` changed to `void`  
- `do_gettimeofday` → `ktime_get`  
- `of_get_named_gpio_flags` → `of_get_named_gpio`  
- `late_initcall` (give regulator / SPI a bit of time)  
- stub `set_lcd_reset_gpio_keep_high` (panel reset belongs to DRM)  
- MP / EXT_PROC / PROC off; keep `WAKEUP_GESTURE` only for the regulator code path  
- Global `struct nvt_ts_data *ts` **must not be deleted** (fw_update uses `extern`)

---

## 5. Failed-experiment timeline (so we do not walk it again)

In chronological order, not “what looked reasonable”.

### 5.1 Only gpio-keys: red NO TOUCH DEVICE

Userspace test painted fb0. At the time `/dev/input` had only volume keys. Banner red **NO TOUCH DEVICE** / **DRIVER NOT PROBED**. This is “no device node”, not “device exists but does not report points”.

### 5.2 First time the driver was attached: chip ID all `0xFF`, `error -22`

pinmux still gpio. Reads all 1s. Probe `late_initcall` ~3.0 s, panel init ~3.8 s — easy to misread as “bias not stable”. **`cat` gpio6–9 function first.**

### 5.3 After sleep pinctrl changed to qup02: still all `0xFF`

Pins already `qup02`. The real bug became CS polarity. At this point `mode=4`.

### 5.4 Accidentally deleted global `ts`: vmlinux link failed

`undefined reference to ts`. If the script still packs `boot.img` with the **old Image.gz**, it boots the previous kernel and logs no longer match the source. Before packing, confirm `Image.gz` timestamp / size changed.

### 5.5 `EPROBE_DEFER` 20 times

Idea: wait for panel LCDB. On device it later turned out that once CS was correct, **the first probe (~3.2 s) already read chip ID**; no defer needed. Defer can stay as insurance, but do not treat it as the root cause.

### 5.6 After CS was correct: `NVTCapacitiveTouchScreen` present, firmware filename empty, `-22`

`request_firmware("")` → `-EINVAL`. At the time we thought `BOOT_UPDATE_FIRMWARE_NAME` and `EXTRA_FIRMWARE` filenames disagreed. The real cause was **struct misalignment**; `boot_update_firmware_name[]` was reading some other field. Hardcoding the tianma filename as a string literal bypassed the “empty name”, loaded the bin, then Oops after the parser.

### 5.7 Firmware bin already loaded: parser finished partition=15, then Oops

```
Workqueue: nvt_fwu_wq Boot_Update_Firmware
pc : nvt_update_firmware+0x954
Code: ... f9418800 (b9400000)    ; ldr x0,[x0,#784]; ldr w0,[x0]
x0=0
ESR: 0000000096000005           ; data abort, translation fault
```

Disassembly corresponds to:

```c
nvt_write_addr(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_RESET_COMPLETE, 0x00);
```

`ts->mmap == NULL`. Also wrongly took the `hw_crc==0` `nvt_download_firmware()` path (NT36672A is actually `hw_crc=1`). Root cause: CONFIG_FB layout mismatch.

After Oops, sshd often dies. Ping still works. A human must enter fastboot then `fastboot boot`.

### 5.8 63 KB transfers (secondary, hidden by the Oops)

`NVT_TRANSFER_LEN = 63*1024` is prepared for DMA. On a FIFO-only controller it should be treated as harmful. Even after struct alignment, keep small chunks. Current value is 32.

---

## 6. Three decisive fixes (code level)

### 6.1 CS = SPI_MODE_0, drop `spi-cs-high` from DTS

`nt36xxx.c` probe:

```c
ts->client->bits_per_word = 8;
/* 下游用 MODE_0 覆盖 DT spi-cs-high。mode=4 在 ginkgo 上 MISO 全 0xFF。 */
ts->client->mode = SPI_MODE_0;
```

DTS `touchscreen@0` must **not** have `spi-cs-high`.

### 6.2 Header-wide `#undef CONFIG_FB`

`nt36xxx.h` **before** `struct nvt_ts_data`:

```c
/* 必须在头文件里 undef，让 nt36xxx.c 与 nt36xxx_fw_update.c 布局一致。 */
#undef CONFIG_FB
```

The original `#undef` in `nt36xxx.c` can stay as a comment-style defense, but it must not live only in the `.c`.

### 6.3 Small-chunk SPI + explicit firmware name

`nt36xxx.h`:

```c
#define NVT_TRANSFER_LEN  (32)
#define BOOT_UPDATE_TIANMA_FIRMWARE_NAME  "novatek_ts_tianma_fw.bin"
```

In probe, before queueing FW work:

```c
strscpy(ts->boot_update_firmware_name, BOOT_UPDATE_TIANMA_FIRMWARE_NAME,
        sizeof(ts->boot_update_firmware_name));
```

`Boot_Update_Firmware` falls back to the same literal if the name is empty (guard against misalignment recurring).

---

## 7. Acceptance log (full NVT sequence)

One `fastboot boot` after struct alignment (2026-08-17 23:24 `boot.img`):

```
[    2.839] nvt_driver_init: start
[    2.850] TP info: [Vendor]tianma [IC]nt36672a
[    2.882] nvt_ts_probe: start
[    2.893] mode=0, max_speed_hz=8000000
[    2.907] novatek,reset-gpio=599
[    2.921] novatek,irq-gpio=600
[    2.934] SWRST_N8_ADDR=0x03F0FE
[    2.947] SPI_RD_FAST_ADDR=0x03F310
[    2.961] get/put regulator : 1
[    3.186] buf[1]=0x0A, buf[2]=0x00, buf[3]=0x00, buf[4]=0x72, buf[5]=0x66, buf[6]=0x03
[    3.227] This is NVT touch IC
[    3.286] request irq 102 succeed
[    3.300] fw name='novatek_ts_tianma_fw.bin'
[    3.333] nvt_ts_probe: end
[    8.537] filename is novatek_ts_tianma_fw.bin
[    8.555] fw_need_write_size = 110592(0x1b000), NVT end flag
[    8.576] ovly_info=0 ilm_dlm_num=2 ovly_sec_num=0 info_sec_num=13 partition=15
[    8.978] Update firmware success! <376682 us>
[    8.996] FW type is 0x01
[    9.008] PID=591F
```

Userspace:

```
/dev/input/event0   gpio-keys                     no
/dev/input/event1   NVTCapacitiveTouchScreen      YES  0..1079 x 0..2339
```

IRQ being 0 when nobody is touching is normal:

```
102:  0  0  0  0  0  0  0  0  msmgpio  88 Edge  NVT-ts
```

After tapping the screen that line should increment. Visualization: `python3 /usr/local/sbin/ginkgo-touch-test.py` (volume- exits, volume+ clears the screen).

Chip ID `0x0A,0x00,0x00,0x72,0x66,0x03` hits in `nt36xxx_mem_map.h`:

```c
{.id = {0x0A, 0xFF, 0xFF, 0x72, 0x66, 0x03}, .mask = {1, 0, 0, 1, 1, 1},
 .mmap = &NT36672A_memory_map, .hwinfo = &NT36672A_hw_info},  /* hw_crc = 1 */
```

---

## 8. How to use the visualization test

Host (phone already on SSH):

```bash
./scripts/usb-connect.sh
SSHPASS=$GINKGO_ROOT_PASSWORD sshpass -e ssh -b 192.168.7.1 root@192.168.7.2 \
  'setsid python3 /usr/local/sbin/ginkgo-touch-test.py </dev/null >/tmp/touch-test.log 2>&1 &'
```

Do not run it in the SSH foreground (disconnecting SSH may take the process with it). `setsid` + redirect.

The script is `scripts/ginkgo-touch-test.py`, packed into the initramfs overlay. Recognition rules include `nvt` / `touch` in the name, and ABS_MT.

---

## 9. Known leftovers (touch works; these need not block reporting points)

| Item | Notes |
|------|-------|
| `SPI driver NVT-ts has no spi_device_id` | missing `spi_device_id` table, harmless |
| Several `-Wmissing-prototypes` | downstream style, not cleaned up |
| `NVT_TRANSFER_LEN=32` | FIFO-safe; can grow later if GPI DMA is enabled |
| Gesture / sleep / double-tap | `WAKEUP_GESTURE` compiled in, product behavior not accepted |
| lab/ibb shared with panel | do not shut LCDB dead on the touch remove path |
| `fastboot boot` not persisted | power-off / normal reboot returns to the old kernel in the partition |
| WiFi | remaining P4 item |
| ttyMSM0 still deferred | unrelated to touch |

---

## 10. SOP for agents (touch regression)

1. After every reboot / `fastboot boot`, first run `./scripts/usb-connect.sh`  
2. `dmesg | grep NVT-ts`  
   - Must see `mode=0`  
   - Must see a non-`0xFF` chip ID  
   - Must see `Update firmware success`  
   - Must **not** see `Internal error: Oops` / `mmap` null pointer  
3. `cat /sys/class/input/event*/device/name` contains `NVTCapacitiveTouchScreen`  
4. Visualize with `ginkgo-touch-test.py`; banner should be green **TOUCH OK**  
5. If all `0xFF` again: first check whether gpio6–9 are `qup02`, then whether `mode=` has become 4 again  
6. If chip ID is correct but no success: check Oops, check `NVT_TRANSFER_LEN`, check whether both `.c` files see `#undef CONFIG_FB`  
7. Verify with `fastboot boot out/boot.img`, **do not** `fastboot flash boot`  
8. Link error `undefined reference to ts` = global `ts` was dropped; do not mix-pack an old Image  

Flash loop:

```bash
source scripts/env.sh
make -C "$KERNEL_SRC" O="$KBUILD_OUTPUT" -j"$(nproc)" Image.gz dtbs
cp -f "$KBUILD_OUTPUT/arch/arm64/boot/Image.gz" out/Image.gz
cp -f "$KBUILD_OUTPUT/arch/arm64/boot/dts/qcom/sm6125-xiaomi-ginkgo.dtb" out/sm6125-xiaomi-ginkgo.dtb
./scripts/build-bootimg.sh
# 手机在 Ubuntu 且 SSH 好：
./scripts/reboot-fastboot.sh
fastboot boot out/boot.img
# Oops 后 SSH 死了：让用户按键进 fastboot，再 fastboot boot
```

---

## 11. Contrast with display bring-up

| | Display P3 | Touch (this file) |
|--|------------|-------------------|
| False positive | backlight on, DCS complete, INTF 60fps | event node present, IRQ=0, chip ID present |
| Conclusive check | DSI TPG on screen; FIFO `0x1010` | `mode=0` + non-0xFF ID + `Update firmware success` + finger produces points |
| Mainline default trap | prog fetch 24 lines vs VFP 10 | `spi-cs-high` DT vs driver MODE_0; CONFIG_FB struct |
| Shared hardware | LCDB / L9A / GPIO90 reset | same LCDB / L9A; reset is GPIO87 |
| What to ask the user to look at | magenta after FIFO healthy | tap the screen after green TOUCH OK |

Display must work first: the test program paints `/dev/fb0`. Touch does not depend on the DRM composition path, but does depend on LCDB analog power and SPI pins.

---

*End of record. Touch acceptance date 2026-08-17.*
