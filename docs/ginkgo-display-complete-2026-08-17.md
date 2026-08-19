**Language:** English | [简体中文](zh-CN/ginkgo-display-complete-2026-08-17.md)

# Redmi Note 8 (ginkgo) mainline display: from “backlight, no image” to magenta on screen

> Device: Xiaomi Redmi Note 8 · codename **ginkgo** · SoC **SM6125 (trinket)** · serial `<serial>`  
> Panel: Tianma **NT36672A** · 1080×2340@60 · 4-lane MIPI DSI **Video** · `non_burst_sync_event`  
> Backlight: Kinetic **KTD3136** @ I2C `0x36`, HWEN = PMI632 GPIO6  
> Stack: **mainline DRM / DPU 5.4 / DSI 6G v2.3 / 14nm PHY**, **do not fall back to simplefb**  
> Acceptance date: 2026-08-17 · user confirmed the panel looked “a bit pink” (「有点粉色」) — magenta framebuffer test pattern  
> This is the full story of **DPU→DSI pixels on screen**: dead ends, decisive experiments, code, registers, and lessons.

**Related docs**

| Doc | Contents |
|-----|----------|
| [ginkgo-display-bringup-methodology.md](./ginkgo-display-bringup-methodology.md) | Earlier stages: LCDB / DCS / PHY / DPMS |
| [ginkgo-fbcon-boot-2026-08-18.md](./ginkgo-fbcon-boot-2026-08-18.md) | Kernel boot log on the panel (fbcon / `console=tty0`) |
| [ginkgo-dsi-err-status5-analysis.md](./ginkgo-dsi-err-status5-analysis.md) | `dsi_err status=5` deep dive |
| [ginkgo-mainline-bringup-chronicle.md](./ginkgo-mainline-bringup-chronicle.md) | Whole-device bring-up timeline |
| [thinking.md](../thinking.md) | Earlier raw register notes |
| [display-bringup-loop.sh](../scripts/display-bringup-loop.sh) | Agent debug SOP |
| [usb-connect.sh](../scripts/usb-connect.sh) | Reconfigure RNDIS after every reboot |
| [reboot-fastboot.sh](../scripts/reboot-fastboot.sh) | Enter fastboot from Ubuntu |

---

## 0. One-sentence conclusion

The panel finally showed pixels not because we “added a bit more MDP clock” or “cycled DSI one more time”, but because:

1. **DSI generating its own pixels (TPG) already worked** — the panel, PHY, HS, and full 1080 width were fine.  
2. **DPU INTF and the DSI MDP FIFO were fighting while INTF fed pixels** — FIFO stuck at `0xcccc1019` (VIDEO_MDP overflow and underflow at the same time).  
3. The switch that caused the fight is **INTF programmable fetch (VFP prefetch)**: catalog default 24 lines, panel VFP only 10 lines. The prefetch window landed on DSI BLLP (which does not consume MDP pixels). The first VFP blew the FIFO.  
4. After **disabling INTF_1 prog fetch**, and **HS-cycling DSI first (INTF off) then enabling the timing engine**: `LANE=0x1f00`, `FIFO=0x1010`, INTF 60fps, no `dsi_err`. Magenta fb visible to the user.

---

## 1. Lessons (for the next SoC / panel bring-up)

### 1.1 Split the link into independently falsifiable segments

This display chain has at least five segments. Each has its own hard evidence. Do not convict an earlier segment with a later segment’s symptom:

```
backlight (KTD3136)  ≠  panel DCS/LP  ≠  DSI HS/PHY  ≠  DPU INTF pixels  ≠  fb/DPMS content
```

| Segment | False positive | True falsifier |
|---------|----------------|----------------|
| Backlight | Lit, so you assume there is an image | I2C id, brightness sysfs |
| DCS | `panel init complete` so you assume video works | Proves LP commands only |
| DSI HS | `INTF_FRAME_COUNT` 60fps so you assume there is an image | The timing engine runs even with no pixels |
| DPU | underrun=0 so you assume INTF is fed | When DSI drops HS, underrun can be near 0 |
| Content | fb all-black + healthy link = user still reports black | Write magenta/white, and **FIFO must be healthy first** |

The most valuable experiment this time was **DSI TPG**: generate pixels inside the DSI controller and fully bypass DPU. Once a checkerboard / solid green hit the panel, “bad panel / wrong lane map / missing columns” could all be closed. The problem shrank to **INTF→DSI MDP input**.

### 1.2 Registers are more honest than logs, but you must decode sticky bits

`FIFO_STATUS` overflow/underflow are **sticky**. Reading `0xcccc1019` only means “overflow and underflow happened at some point”, not that this microsecond is in that state. Correct use:

1. Record the current value  
2. W1C (write the value you just read back)  
3. Wait 1–2 frames and read again  

If it is immediately `0xcccc` again after W1C, that is a **sustained mismatch**. If it becomes `0x1010`, it was only a start-up transient.

Same for `LANE_STATUS`: in video mode, lanes return to LP-11 during BLLP. A single read of `0x1f1f` may just be blanking. Sample continuously, or combine with `CLK_STATUS` `VID_PCLK_ACTIVE`.

### 1.3 Mainline defaults need not hold for this SoC + this panel

Mainline DPU catalog `prog_fetch_lines_worst_case = 24` is correct for many flagship panels (large VFP). ginkgo’s Tianma has VFP=10. The formula becomes “use the entire VFP as the fetch window”. INTF thinks it is buying MDP time; DSI thinks this interval is BLLP and must not have MDP pixels — both sides are right; together they are wrong.

Same class of traps:

- `_dpu_core_perf_calc_clk()` used `hdisplay` instead of `mode->clock`, computed 160 MHz < pclk 183 MHz.  
- `clk_inefficiency_factor=220` made `mode_valid` compute 402.6 MHz > 400 MHz cap, **deleted 1080x2340 from the connector mode list**, which showed up as “no fb0, all DSI clocks 0”. Display was not broken; **the mode was rejected by policy**.  
- 14nm PHY has **no** HS-request generator; `LANE_CTRL` bit24 must be 1 (controller issues HS). Mainline clears bit24 on chips that “have PHY HS req”.  
- DSI 6G registers are **+4** vs xml. Miss that +4 and you read `LANE_CTRL` as `LANE_STATUS`.

### 1.4 Change one variable at a time; “cycle once more while we are here” creates new root causes

The longest detours stacked two problems together:

- To stabilize HS, a **20ms SOFT_RESET** of DSI while INTF was **already spraying pixels**. HS settled; FIFO was permanently `0xcccc`.  
- To stop VID_PCLK gating, `DYNAMIC_FORCE_ON` was left on `CLK_CTRL`. HS dropped after ~0.5s.  
- To clear INTF underrun, inefficiency factor was pulled to 220, which exceeded `max_core_clk` and the whole modeset vanished.

Reproduce with a live poke (`/dev/mem`) first, then write it into the kernel. Kernel “auto TPG at boot” is an experiment only, not a product path.

### 1.5 Do not use the user as an oscilloscope

Asking the user to look at the panel while FIFO is still `0xcccc` / `0x5555` always yields “B: backlight, fully black”. Wait until `LANE=0x1f00` and FIFO holds `0x1010`, then write magenta and ask — one look is enough to classify.

### 1.6 The debug channel itself is part of bring-up

After every `fastboot boot` / reboot:

- Host `enx*` interface name changes; **must run** `./scripts/usb-connect.sh` first  
- SSH: `root@192.168.7.2`, password `$GINKGO_ROOT_PASSWORD`, host `192.168.7.1`  
- Enter fastboot: `./scripts/reboot-fastboot.sh`  
- Verify with `fastboot boot out/boot.img`; **do not** `fastboot flash boot` unless explicitly asked to write the partition  
- Fastboot stuck: `echo $GINKGO_ROOT_PASSWORD | sudo -S usbreset 18d1:d00d`

**Never** mmap past the INTF_1 block (base `0x5e6b800`, length `0x2c0`). Reading `INTF+0x6a8` hangs the SoC/USB. INTF mmap must start at page-aligned `0x5e6b000`, but only access `+0x800`–`+0xAC0`.

### 1.7 Record failed experiments too (so the next pass does not repeat them)

See section 8. Repeating “disable DATA_HCTL / enable widebus / write INTF TPG / CTL FLUSH” produces no new information; it only drops HS.

---

## 2. Goals, acceptance, non-goals

### 2.1 Goal

On mainline Linux, walk the full **DRM → DPU INTF_1 → DSI0 → 14nm PHY → NT36672A** path, with pixels visible to the user.

### 2.2 Quantitative acceptance (Level B)

| Metric | Address / source | Success threshold | 2026-08-17 measured |
|--------|------------------|-------------------|---------------------|
| DSI HS | `LANE_STATUS` `0x5e940a8` | Mostly `0x1f00`, not permanently `0x1f0f`/`0x1f1f` | `0x1f00` stable |
| LANE_CTRL | `0x5e940ac` | bit24=1, bit28=0 | `0x01000000` |
| FIFO | `0x5e9400c` | `0x1010`; no `0xcccc`/`0x5555` in the high 16 bits | `0x1010` for several seconds |
| INTF frame rate | `INTF_FRAME_COUNT` `0x5e6b8ac` | +45–75 in 1s | **60** |
| Encoder vsync | `encoder-0/status` | Reference; may be below 60 (IRQ gated) | ~11 (IRQ), does not affect image |
| INTF underrun | same | Not once per frame | **0** |
| dmesg | whole run | No runtime `dsi_err` | **0** after boot |
| DPMS | `fb0/blank`, `card0-DSI-1/dpms` | `0`, `On` | yes |
| User | look at panel | Can recognize the test color | **“a bit pink”** |

`INTF_FRAME_COUNT` at 60fps **alone** does not prove there is an image; the timing engine can still run on a black screen.  
encoder `frame_done_cnt` is actually `frame_done_timeout_cnt`; 0 means **no timeout**, not “no completed frames”.

### 2.3 Non-goals

- Do not fall back to simple-framebuffer  
- Do not import the whole Android `dsi-staging` tree into mainline  
- Do not ask the user to look at the panel while FIFO is unhealthy  
- Do not `fastboot flash boot` (this entire run used `fastboot boot`)

---

## 3. Hardware datapath

```
  GEM / fb0 (XR24, 1080×2340, stride 4352)
           │
           ▼
  SSPP VIG0 ──▶ LM ──▶ PP ──▶ CTL_0 ──▶ INTF_1 (0x5e6b800)
                                           │  pclk domain, 1 pixel / pclk
                                           ▼
                                      DSI Host (0x5e94000, 6G +4)
                                           │  VIDEO_MDP FIFO
                                           │  TPG can bypass MDP here
                                           ▼
                                      14nm PHY (0x5e94400)
                                           │  4-lane HS
                                           ▼
                                      NT36672A 1080×2340
```

Clocks (measured `clk_summary` when pixels were on screen):

| Clock | Frequency | Notes |
|-------|-----------|-------|
| `disp_cc_mdss_pclk0_clk` | 183012000 | pixel clock = htotal×vtotal×60 |
| `disp_cc_mdss_byte0_clk` | 137259000 | bitclk/8 |
| `disp_cc_mdss_byte0_intf_clk` | 68629500 | 14nm: byte/2 |
| `disp_cc_mdss_mdp_clk` | 400000000 | top OPP (factor 218 requests ~399 MHz) |

Panel timing (aligned with downstream dtsi):

| | Value |
|--|-------|
| hdisplay / vdisplay | 1080 / 2340 |
| HFP / HPW / HBP | 90 / 2 / 120 → htotal **1292** |
| VFP / VPW / VBP | 10 / 3 / 8 → vtotal **2361** (TOTAL in registers uses htotal-1 / vtotal-1) |
| pclk | 183.012 MHz |
| Pixel format | RGB888, 4-lane |
| traffic | `non_burst_sync_event` |
| BLLP | `BLLP_POWER_STOP` + `EOF_BLLP_POWER_STOP` (VID_CFG0=`0x9130`) |

DSI vs INTF horizontal windows (checked; not off-by-one):

| | Register | Decode |
|--|----------|--------|
| DSI `ACTIVE_H` | `0x04b2007a` | start=122, end=1202 **exclusive** → 1080 |
| INTF `DISPLAY_HCTL` | `0x04b1007a` | start=122, end=1201 **inclusive** → 1080 |
| DSI `TOTAL` | `0x0938050b` | h=1291, v=2360 (i.e. totals−1) |
| INTF `HSYNC_CTL` | `0x050c0002` | period=1292, pulse=2 |

---

## 4. User look-at-the-panel timeline (honest record)

Ask the user to look only after **we already have a register conclusion**. A few reports sliced the problem into completely different layers:

| Code | What the user saw | Registers at the time | Real meaning |
|------|-------------------|-----------------------|--------------|
| **C** | No backlight at all | KTD3136 not probed / HWEN | Backlight IC, unrelated to DSI |
| **B** | Backlight on, picture fully black | HS or FIFO bad, or fb all 0 | Link or content |
| **A** | **DSI TPG checkerboard**, black strip on the far right | TPG on, `FIFO=0x1010` | Full 1080 panel width works; black strip is checkerboard tiles not aligning to 1080 |
| **A′** | After switching to DSI full-screen solid green, **entire panel green** | same | Right edge is not missing columns; it is the TPG pattern |
| **Final** | **A bit pink** | TPG **off**, `FIFO=0x1010`, magenta fb | **DPU pixels on screen** |

Fix for C: `linux/drivers/video/backlight/ktd3136.c` + DTS `kinetic,ktd3136` @ `0x36`, `CONFIG_BACKLIGHT_KTD3136=y`. On device `id=0x18`, brightness 1024/2047.

---

## 5. Phase recap: layers already fixed before FIFO

These landed from early to mid 2026-08. Details in methodology / thinking.md. Without them the later TPG/FIFO experiments could not run.

| Layer | Problem | Fix |
|-------|---------|-----|
| L1 bias | PMI632 LCDB | Mainline LCDB regulator |
| L1 PHY | LDO written as `0x3c` | `dsi_14nm_phy_ldo_cntrl()` → standalone `0x1c` |
| L1 PHY | Clamp not released after UEFI | `dsi_phy_clamp` @ `0x5e01400` |
| L2 DCS | Commands sent before host ready | manager: panel first, then `host_enable` |
| L2 HS | Mainline clears `LANE_CTRL` bit24 | 14nm must have bit24=1; panel `CLOCK_NON_CONTINUOUS` to avoid bit28 |
| L2 clocks | `byte_intf` not going through `byte0_div` | DT + `dispcc-sm6125` + host `clk_set_rate(byte_intf_div)` |
| L4 DPMS | deferred fbdev `blank=4` | `display-unblank.service` (**do not** `restore_fbdev_mode` in probe; it hangs) |

At this point: DCS `power mode 0x9c`, INTF 60fps, backlight can turn on, user can still be **B**.

---

## 6. Decisive experiment: DSI TPG (bypass DPU)

### 6.1 Method

debugfs `dsi_tpg` (or write directly):

- `TEST_PATTERN_GEN_CTRL` EN=bit0  
- Checkerboard used `VID_MDSS_GENERAL_PATTERN` + checkered  
- Solid green: `VIDEO_INIT_VAL=0x00ff00`, `VIDEO_PATTERN_SEL=VID_FIXED`

**Must HS-cycle once after TPG is enabled**, otherwise lanes may stay in STOP. Healthy combination:

```
LANE=0x1f00   FIFO=0x1010   TPG=EN
```

### 6.2 Results

| State | LANE | FIFO | User |
|-------|------|------|------|
| TPG on + HS cycle | `0x1f00` | `0x1010` | checkerboard / full-screen green |
| TPG off, no reset | often `0x1f1f` | `0x55551011` | black (HS FIFO empty) |
| TPG off, cycle again | `0x1f00` | `0xcccc1019` | black (MDP mismatch) |

`0xcccc1019` decode (`dsi.xml` FIFO_STATUS, 6G address `0x5e9400c`):

| Bit | Name | Meaning |
|-----|------|---------|
| 0 | VIDEO_MDP_OVERFLOW | DSI video engine MDP input overflow |
| 3 | VIDEO_MDP_UNDERFLOW | underflow at the same time |
| 12 | DLN0_LP_EMPTY | Often appears with `0x1010`; treat as part of “idle/healthy” |
| 18/19, 22/23, 26/27, 30/31 | all four lanes HS overflow **and** underflow | packer/beat fills and empties within a single line |

**Conclusion (at the time):** DSI→panel OK; DPU INTF→DSI MDP **out of sync**.  
**Wrong conclusion (later overturned):** “must be MDP under-clocked” or “must be 1ppc/2ppc packing”. Under-clocking can explain INTF underrun; it **cannot** by itself explain TPG good and DPU forever `0xcccc`.

Product path: **do not** auto-enable TPG at boot. TPG is an oscilloscope.

---

## 7. MDP clock: a real problem, but not the FIFO root cause

### 7.1 Mainline computed too low

`_dpu_core_perf_calc_clk()` originally used `vtotal * hdisplay * vrefresh` → **160 MHz**, below INTF pclk 183 MHz. INTF underran almost every frame.

Changed to:

```c
mode_clk = (u64)mode->clock * 1000;  /* 183012000 */
```

Then multiply by `clk_inefficiency_factor`. factor 105 → request 192 MHz → OPP lands at **256 MHz**, underrun still near once per frame. debugfs `perf_mode=1` pulled it to **400 MHz**, underrun **cleared**, but FIFO was still `0xcccc1019`.

So: 400 MHz is the condition for “INTF is fed”, **not** the condition for “DSI MDP FIFO healthy”.

### 7.2 The factor 220 trap (entire modeset evaporates)

`dpu_crtc_mode_valid()`:

```c
adjusted = mode->clock * factor / 100;          /* kHz */
if (max_core_clk_rate < adjusted * 1000)
    return MODE_CLOCK_HIGH;
```

SM6125 has no `has_3d_merge`, so no divide by 2.

| factor | Request | Result |
|--------|---------|--------|
| 105 | 192 MHz | mode present, OPP 256 MHz |
| 220 | **402.6 MHz** | **> 400 MHz → mode deleted** → `/sys/class/drm/card0-DSI-1/modes` empty, no fb0, DSI clock enable_count=0 |
| **218** | 398.97 MHz | mode present, OPP **400 MHz** |

Log `Cannot find any crtc or sizes` with factor 220 is **literally no usable mode**, not a sporadic fbdev glitch.

Catalog is now `clk_inefficiency_factor = 218`.

### 7.3 An easy-to-misread related symptom

| Condition | encoder underrun |
|-----------|------------------|
| HS dropped `0x1f1f`, DSI not pulling pixels | can be near **0** (INTF spinning empty) |
| HS `0x1f00` but FIFO `0xcccc` | **underrun every frame** (DSI pulling pixels on the wrong beat) |
| FIFO `0x1010` + 400 MHz + no prog fetch | **0** |

Do not infer FIFO health from “underrun=0”.

---

## 8. Tried, ineffective, do not repeat

The following live pokes or kernel changes **did not** get the DPU-path FIFO to `0x1010`. Repeating them only drops HS or wastes a `fastboot boot`.

| Experiment | Result |
|------------|--------|
| Disable `DATA_HCTL_EN` (INTF_CFG2=0) | FIFO still bad, HS drops more easily; special-case already withdrawn from `dpu_hw_intf.c` |
| INTF 1ms / 20ms off/on | HS → `0x1f1f` |
| INTF off → DSI cycle → INTF on (**prog fetch still on**) | immediately `LANE=0x1f00 FIFO=0xcccc` |
| Write `INTF_MUX` to 0 | no help (low bits of `0xf0000` = PP0 is expected) |
| `PANEL_FMT=0x213f` | reads back still `0x2100` (bpc bits dropped by HW) |
| DSI `DATABUS_WIDEN` bit25 | 6G v2.3 **will not take the write** |
| INTF widebus only | worse |
| INTF TPG registers | not wired on this SoC; write reads back 0 |
| VID without BLLP power stop (`0x0130`) | cannot even bring HS up |
| CTL `FLUSH=0xffffffff` / random `START` | HS drops |
| Write `DISP_INTF_SEL` as DSI | basically NOP on DPU 5.x |
| Extra HS cycle after success (settle) | often knocks a good `0x1f00` back to STOP, or `0x1010` to `0xcccc` |
| Leave `DYNAMIC_FORCE_ON` on CLK_CTRL | ~0.5s HS drop, `CLK_STATUS` loses DYN_PCLK/BYTECLK |
| 1µs SOFT_RESET instead of 20ms | momentary `0x1f00`, FIFO quickly `0xcccc`, HS unstable |
| Disable BLLP / change traffic mode | did not improve MDP FIFO |

**mmap red line:** do not read `INTF_1+0x6a8`. INTF_STATUS is `0x26C`, FRAME_COUNT is `0x0AC`. For DSI use a small window `mmap(..., 0x1000, offset=0x5e94000)`.

---

## 9. The real root cause: INTF programmable fetch × DSI BLLP

### 9.1 Mechanism

`programmable_fetch_get_num_lines()` in `dpu_encoder_phys_vid.c`:

- catalog `prog_fetch_lines_worst_case = 24`  
- lines SOF can absorb = VBP + VPW = 8+3 = **11**  
- still short 13 lines, but VFP is only **10**, so **the entire VFP is marked as the fetch window**  
- `INTF_CONFIG` bit31 = `PROG_FETCH_START_EN`, measured `0x80800000`

DSI video mode uses **BLLP** during VFP (`VID_CFG0=0x9130` includes BLLP_POWER_STOP): in this interval it **does not take pixels from the MDP FIFO**, it only sends blanking packets.

If INTF prefetch pushes “next-frame pixels” onto the INTF→DSI CDC/FIFO early:

- VFP: DSI does not consume → **VIDEO_MDP overflow**  
- next-frame active: FIFO already scrambled → **underflow**, lane HS FIFOs overflow+underflow together → `0xcccc1019`  
- timing: FIFO can be `0x1010` right when INTF enables (first VFP not hit yet), then **~16–21ms later** (end of first frame) becomes `0xcccc`

This matches 1ms sampling exactly:

```
normal CFG 0x80800000
   0ms  LANE 0x1f00 FIFO 0x1010
  21ms  LANE 0x1f00 FIFO 0xcccc1019
```

TPG does not use the MDP FIFO, so TPG is always `0x1010`.

### 9.2 Decisive live poke

INTF off, clear `INTF_CONFIG` bit31 (CFG becomes `0x800000`), DSI 20ms cycle, then INTF on:

```
noprog CFG 0x800000
   0ms  …
   1ms  LANE 0x1f00 FIFO 0x1010
   (no further change within 40ms)
```

Stretched to 2s: FIFO stays `0x1010`. Still `0x1010` after W1C.

Clear only bit23, keep bit31: breaks on the first frame period.  
Clear both: FIFO does not look like `0xcccc`, but HS chatters between STOP/HS (do not casually touch bit23).

### 9.3 Code fix

`linux/drivers/gpu/drm/msm/disp/dpu1/catalog/dpu_5_4_sm6125.h` **change INTF_1 (DSI) only**:

```c
.prog_fetch_lines_worst_case = 0,
```

INTF_0 (DP) stays 24. ginkgo does not use DP.

After prefetch is off, VBP+VPW=11 lines must be enough for INTF to fetch the first line from memory. MDP already runs at 400 MHz (~2.2× pclk); measured **underrun=0**.

If a later panel has a large VFP, prefetch can be turned back on; **do not** use 24 on this VFP=10 NT36672A.

---

## 10. Boot order: DSI HS-cycle with INTF off

### 10.1 Wrong order (FIFO permanently 0xcccc)

```
enable_timing(1)          // INTF starts spraying pixels
host_enable()
  op_mode_config ENABLE
  dsi_ginkgo_hs_cycle()   // 20ms SOFT_RESET, INTF still spraying
  [optional] cycle again  // settle, often worse
```

On 14nm the first `DSI_EN` often leaves data+clk in STOP (`0x1f1f`). ENABLE flip + `HS_REQ_SEL_PHY` + `DYNAMIC_FORCE_ON` + SOFT_RESET can pull lanes to `0x1f00`.  
But **resetting 20ms while INTF is running** ≈ dumping 1.2 frames of pixels into a dead MDP FIFO.

### 10.2 Correct order (matches the live experiment)

```
host_enable()             // INTF still 0
  hs_cycle ×N             // LANE often 0x1f1f, FIFO 0x11111010, normal
enable_timing(1)          // immediately LANE 0x1f00, FIFO 0x1010 (holds when no prog fetch)
```

`dpu_encoder_kickoff()` is now: `msm_dsi_manager_host_enable()` first, then `handle_post_kickoff()` (which does `enable_timing(1)`).

Boot log will show:

```
ginkgo DSI after EN tries=9 ... LANE_ST=0x1f1f FIFO=0x11111010 CLK_ST=0x4343
```

`tries=9` means INTF is not on yet; 8 cycles cannot pull HS up — **expected**. Once timing enables, samples are `0x1f00` / `0x1010`. Do not enable INTF early just to see `0x1f00` inside `host_enable`.

### 10.3 `dsi_ginkgo_hs_cycle()` essentials

- Pull `DSI_EN` low → `CLK_CTRL |= DYNAMIC_FORCE_ON` → bit24=1, clear DLN force stop  
- Hold `SOFT_RESET` for **20ms** (1µs is not enough for stable HS)  
- **Must write CLK_CTRL back to the original value** (usually `0x23f`); do not leave DYNAMIC_FORCE_ON  
- Then `DSI_EN=1`, wait another ~20ms  

Video-mode `dsi_err_worker` **must not** `dsi_sw_reset` again (that wrecks a FIFO that is already scanning out); only clear sticky bits.

---

## 11. Code change list (scanout-related)

Paths are relative to `linux/`. Only items directly related to this scanout. **Earlier LCDB/clamp/LDO are in methodology.**

| File | Change | Why |
|------|--------|-----|
| `drivers/gpu/drm/msm/disp/dpu1/catalog/dpu_5_4_sm6125.h` | INTF_1 `prog_fetch_lines_worst_case=0` | **FIFO root cause** |
| same | `clk_inefficiency_factor=218` | 400 MHz OPP, without failing `mode_valid` |
| `drivers/gpu/drm/msm/disp/dpu1/dpu_core_perf.c` | `mode_clk = mode->clock * 1000` | do not underestimate pclk via hdisplay |
| `drivers/gpu/drm/msm/disp/dpu1/dpu_encoder.c` | kickoff: DSI host_enable first, then `enable_timing` | avoid 20ms reset while INTF is spraying |
| `drivers/gpu/drm/msm/dsi/dsi_host.c` | `LANE_CTRL` bit24=1; `dsi_ginkgo_hs_cycle()`; up to 8 ENABLE retries after kickoff; video err does not `sw_reset` | 14nm HS |
| `drivers/video/backlight/ktd3136.c` + DTS | KTD3136 | user symptom C |
| rootfs-overlay `display-unblank.service` | unblank fb0 at boot | avoid `blank=4` fake black screen |

Build:

```bash
./scripts/build-kernel.sh && ./scripts/build-bootimg.sh
./scripts/reboot-fastboot.sh
fastboot boot out/boot.img
```

---

## 12. Register cheat sheet (ginkgo on device, DSI 6G = xml+4)

| Item | Physical address | When pixels are on screen |
|------|------------------|---------------------------|
| DSI CTRL | `0x5e94004` | `0x1f3` |
| FIFO_STATUS | `0x5e9400c` | **`0x1010`** |
| VID_CFG0 | `0x5e94010` | `0x9130` |
| LANE_STATUS | `0x5e940a8` | **`0x1f00`** |
| LANE_CTRL | `0x5e940ac` | `0x01000000` |
| CLK_CTRL | `0x5e9411c` | `0x23f` (do not leave `0x200b3f`) |
| CLK_STATUS | `0x5e94120` | `0x9047c3` (includes VID_PCLK) |
| TPG CTRL | `0x5e9415c` | `0x4` (off) |
| INTF_1 TIMING_EN | `0x5e6b800` | `1` |
| INTF_CONFIG | `0x5e6b804` | **`0x800000`** (bit31 prefetch **off**; old `0x80800000` blows FIFO) |
| INTF_HSYNC_CTL | `0x5e6b808` | `0x50c0002` |
| INTF_DISPLAY_HCTL | `0x5e6b83c` | `0x4b1007a` |
| INTF_CONFIG2 | `0x5e6b860` | `0x10` (DATA_HCTL) |
| INTF_DISPLAY_DATA_HCTL | `0x5e6b864` | `0x4b1007a` |
| INTF_PANEL_FORMAT | `0x5e6b890` | `0x2100` |
| INTF_FRAME_COUNT | `0x5e6b8ac` | ~60/s |

Sampling script (DSI read-only, avoid INTF mmap landmines):

```bash
./scripts/usb-connect.sh
SSHPASS=$GINKGO_ROOT_PASSWORD sshpass -e ssh -b 192.168.7.1 root@192.168.7.2 'python3 -u -c "
import mmap,os,struct,time
fd=os.open(\"/dev/mem\",os.O_RDWR|os.O_SYNC)
d=mmap.mmap(fd,0x1000,mmap.MAP_SHARED,mmap.PROT_READ,offset=0x5e94000)
r=lambda o: struct.unpack_from(\"<I\",d,o)[0]
print(hex(r(0xa8)), hex(r(0x00c)))
time.sleep(1)
print(hex(r(0xa8)), hex(r(0x00c)))
"'
```

Magenta test (stride 4352, BGRA):

```python
w,h,stride=1080,2340,4352
px=b"\xff\x00\xff\x00"  # magenta
row=px*w
with open("/dev/fb0","r+b",0) as f:
    for y in range(h):
        f.seek(y*stride); f.write(row)
```

---

## 13. 2026-08-17 acceptance record

After boot (prog fetch off + new kickoff order), SSH:

```
panel init complete
ginkgo DSI after EN tries=9 ... LANE_ST=0x1f1f FIFO=0x11111010   # INTF not yet enabled, expected
fb0: msmdrmfb
blank=0  dpms=On
INTF CFG=0x800000 EN=1
LANE=0x1f00 FIFO=0x1010 TPG=0x4 CLKST=0x9047c3
t+1s / t+2s still 0x1f00 / 0x1010
INTF FRAME_COUNT 1s delta = 60
encoder underrun = 0
dmesg dsi_err count = 0
mdp_clk = 400000000  pclk0 = 183012000
backlight brightness = 1024
```

After writing magenta fb, user’s exact words: **「是的 有点粉色的感觉」**.

Then rebound `vtcon1` (frame buffer device) to 1 to try restoring fbcon/tty1. The link did not regress.

---

## 14. Known leftovers (display works; these need not block scanout)

| Item | Notes |
|------|-------|
| encoder vsync ~11 Hz | INTF hardware 60fps; vsync IRQ may be gated dynamically. Does not affect visible image |
| `frame_done_timeout_cnt=0` | Name sounds like “not completed”; it is a timeout count |
| Boot `tries=9` + `LANE_ST=0x1f1f` | INTF off during host_enable; ignore |
| `lcdb_ncp` sysfs voltage reading | Old issue, unrelated to scanout |
| IOMMU fault `0x5c00xxxx` | leftover UEFI FB |
| PWM vs KTD3136 coexistence | KTD3136 is the current source of truth |
| Cost of prefetch off | Very tight-VBP modes theoretically eat more MDP bandwidth; ginkgo 400 MHz is enough |

Next (not blocking display/touch): Wi-Fi, baking `fastboot boot` into the boot partition (needs an explicit user request), etc.

Full touch record: [ginkgo-touch-complete-2026-08-17.md](./ginkgo-touch-complete-2026-08-17.md).

---

## 15. SOP for agents (display regression)

1. `./scripts/usb-connect.sh`  
2. Check `dmesg`: `panel init complete`; `LANE_ST=0x1f1f` during host_enable is allowed  
3. `blank`/`dpms`, `clk_summary` pclk/mdp  
4. Read `LANE`/`FIFO`, read again after 1s; for 60fps read `INTF+0x0AC` (watch mmap range)  
5. If FIFO is not `0x1010`: **do not ask the user to look**; first check whether `INTF_CONFIG` bit31 got turned back on  
6. Only after FIFO is healthy write magenta/white, then ask the user  

---

## Appendix: common FIFO_STATUS values

| Value | Meaning |
|-------|---------|
| `0x1010` | healthy (TPG or DPU pixels should both look like this) |
| `0x11111010` | four-lane HS empty + LP empty; common when INTF is off |
| `0x55551011` / `0x55551019` | HS FIFO empty, often with STOP |
| `0xcccc1019` | MDP overflow/underflow together + four-lane HS overflow/underflow together → prefetch / dual-engine mismatch |
| `0xdddd1011` | similar, HS already dropped to STOP |
| `0x44441019` | overflow-dominated transitional state |

---

*End of record. Pixels on screen 2026-08-17.*
