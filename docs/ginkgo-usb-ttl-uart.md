**Language:** English | [简体中文](zh-CN/ginkgo-usb-ttl-uart.md)

# Redmi Note 8 (ginkgo) debug UART / USB-to-TTL detailed guide

> Date: 2026-08-05  
> Device: Xiaomi Redmi Note 8 (`ginkgo`, motherboard model LLDM516)  
> Goal: mainline Linux earlycon + serial console (`ttyMSM0` @ 115200)  
> Images: [`docs/images/uart-ttl/`](./images/uart-ttl/)

---

## 0. One-sentence conclusion

Remove the back cover, find the **DBG UART test points TP0003 (TX) / TP0012 (RX)** on the board, cross-wire them to a **1.8V-level** USB-TTL adapter on the PC, baud rate **115200**, and you will see boot logs when flashing mainline.

**The EDL test points circled in red online are not the serial port. Do not solder the wrong pads.**

---

## 1. Why a USB-TTL adapter is needed

At the current mainline boot stage you may have:

- No display yet (DRM not ready)
- adb / USB gadget not up yet
- The kernel dying in the early stage

The only reliable observation channel then is the **SoC debug UART**.  
Project docs already specify:

```
console=ttyMSM0,115200n8
earlycon=msm_serial_dm,0x4a90000
```

Without a serial cable those parameters do nothing.

---

## 2. Hardware and electrical parameters

### 2.1 Signal map (authoritative source: LLDM516 schematic + mainline DTS)

| Item | Value |
|------|-----|
| SoC peripheral | `uart4`, register base `0x04a90000` (QUP0 SE4) |
| Pins | GPIO16 = TX, GPIO17 = RX |
| Schematic names | `DBG_UART_TX` / `DBG_UART_RX` |
| Test points | **TP0003 = TX**, **TP0012 = RX** |
| Linux node | `ttyMSM0` |
| Baud rate | **115200 8N1**, no hardware flow control |
| Logic level | **1.8 V** (typical Qualcomm TLMM; lavender, a sibling device, measured 1.8V) |

There is also a non-debug UART (generally unused):

| TP | Signal | GPIO |
|----|------|------|
| TP0009 | UART_TX | GPIO14 |
| TP0002 | UART_RX | GPIO15 |

### 2.2 Wiring table (must be crossed)

| Phone side | USB-TTL module | Notes |
|--------|--------------|------|
| TP0003 (`DBG_UART_TX`) | **RX / RXD** | Phone transmits → PC receives |
| TP0012 (`DBG_UART_RX`) | **TX / TXD** | PC transmits → phone receives |
| GND (screw hole / ground pad) | **GND** | Common ground is required |
| (none) | **VCC / 3V3 / 5V** | **Do not connect to the phone** |

Diagram:

```
PC ──USB──► [USB-TTL, level switch set to 1.8V]
               │ TXD ────────────►  TP0012 (phone RX)
               │ RXD ◄────────────  TP0003 (phone TX)
               │ GND ─────────────  GND
               │ VCC (leave open; do not connect to the phone)
```

Full wiring diagram: [`images/uart-ttl/ginkgo-usb-ttl-wiring.png`](./images/uart-ttl/ginkgo-usb-ttl-wiring.png)

### 2.3 EDL pads vs UART pads (easy to mix up)

| Type | Purpose | Action | Usable as serial? |
|------|------|------|------------|
| EDL Test Point | Force 9008 flashing | **Short the two pads** | **No** |
| DBG UART TP0003/TP0012 | serial console | Solder wires to USB-TTL | **Yes** |

Local EDL reference photos (orientation only while opening the phone; **not UART solder points**):

- `images/uart-ttl/ginkgo-edl-testpoints.jpg`
- `images/uart-ttl/willow-edl-testpoint.jpg`
- `images/uart-ttl/ginkgo-battery-connector.jpg`

Public material almost never labels `TP0003` / `TP0012` on a real board photo. How to locate them:

1. Search silkscreen `TP0003`, `TP0012` in the **LLDM516 PCB Layout / Boardview**; or  
2. Multimeter: check continuity with power off; after power-up, TX to GND is about **1.8V**, with pulses at boot.

---

## 3. Taobao / Pinduoduo shopping list

Items below are grouped as required / strongly recommended / optional. Prices are typical 2026 ranges; confirm on the product page before ordering.

### 3.1 Required (core)

#### A. 1.8V multi-level USB-to-TTL module (main part)

| Item | Notes |
|----|------|
| **What to search** | `FT232 1.8V 3.3V 5V USB转TTL` or `FT232RL 多电平 串口模块` |
| **Critical spec** | Must support a **1.8V** setting (jumper or DIP switch for 1.8/3.3/5) |
| **Chip** | Prefer **FT232RL / FT232RNL** (stable drivers, good 1.8V support) |
| **Connector** | Type-C or Micro-USB are both fine |
| **Price guide** | About **15–40 yuan**; industrial multi-level about **40–80 yuan** |
| **Check before order** | The product photo must show **1.8V** text or a DIP setting; do not buy 3.3V/5V-only boards |

**Recommended search keywords (copy as-is):**

```
FT232RL 1.8V 3.3V 5V USB转TTL
FT232 多电平 USB转TTL 拨码
USB转TTL 1.8V 高通 串口
```

**Optional brand / model directions (not exclusive):**

- Diustou / various “FT232 multi-voltage” modules (jumper-select 1.8V)
- Ebyte-style multi-level modules (e.g. FT232 series with 1.8/2.5/3.3/5 DIP)
- Any small FT232 board marked “1.8V adjustable”

**Do not buy (unless you add a level shifter):**

```
CH340 USB转TTL          ← most are 3.3V/5V only
PL2303 USB转TTL         ← often fails to read 1.8V
CP2102 3.3V             ← default 3.3V; do not direct-wire unless VIO is changed
Raspberry Pi 3.3V serial cable   ← no 1.8V setting
```

#### B. Dupont / flying-lead jumper wires

| Item | Notes |
|----|------|
| **What to search** | `杜邦线 母对母 20cm` + `杜邦线 公对母` |
| **Use** | Module headers ↔ flying leads soldered on the phone |
| **Price guide** | **3–8 yuan** / strip |

#### C. Enameled wire or very fine hook-up wire (solder to test points)

| Item | Notes |
|----|------|
| **What to search** | `漆包线 0.1mm` or `电子线 30AWG` / `航模线 30AWG` |
| **Use** | Solder to TP0003 / TP0012 / GND (the pads are tiny) |
| **Price guide** | **5–15 yuan** |
| **Tip** | Buy **3 colors** (red/green/black) so TX/RX/GND are easy to tell apart |

---

### 3.2 Strongly recommended (soldering is hard without these)

#### D. Soldering iron + solder + flux

| Item | What to search | Price guide | Notes |
|----|--------|--------|------|
| Temperature-controlled iron | `恒温电烙铁 60W Type-C` or `T12 烙铁` | 40–150 yuan | Hold ~300–350℃ stably |
| Fine solder | `焊锡丝 0.5mm 含松香` or `无铅 0.6mm` | 5–15 yuan | **Fine** is easier than thick |
| Flux | `助焊膏` / `松香助焊剂笔` | 5–12 yuan | Required on test points |
| Desolder / solder removal | `吸锡带` or `吸锡器` | 5–15 yuan | Recovery if you solder the wrong pad |

Phone test points are fragile. **Do not brute-force them with a high-power, uncontrolled iron.**

#### E. Digital multimeter

| Item | Notes |
|----|------|
| **What to search** | `数字万用表` (an entry-level meter is enough) |
| **Use** | ① Confirm GND; ② Check that TX is about 1.8V; ③ Look for shorts |
| **Price guide** | **20–60 yuan** |

#### F. Tear-down tools

| Item | What to search | Price guide |
|----|--------|--------|
| Pry bar / plastic picks | `手机拆机撬棒` | 5–10 yuan |
| Suction cup | `手机屏幕吸盘` | 3–8 yuan |
| Phillips / pentalobe bits | `手机螺丝刀套装` | 10–25 yuan |
| Tweezers | `防静电镊子` | 5–15 yuan |

The Redmi Note 8 back cover is glass held with adhesive. Search `红米 Note 8 拆后盖` for videos; heat (hot-air gun / hair dryer) makes it easier.

---

### 3.3 Optional (higher success rate / fallback)

| Part | What to search | When needed | Price guide |
|------|--------|----------|--------|
| 1.8↔3.3 level shifter | `TXS0102 电平转换` or `TXB0104 模块` | Fallback if you already have a 3.3V USB-TTL | 5–15 yuan |
| Hot-air gun | `迷你热风枪 拆机` | Opening the back cover / shields | 40–100 yuan |
| Magnifier / microscope | `手机维修显微镜` / `台式放大镜` | Reading TP silkscreen | 30–200 yuan |
| High-temp tape | `高温胶带 Kapton` | Strain-relieve flying leads | 5–10 yuan |
| USB extension | `USB Type-C 延长线` | Easier wiring while the phone is open | 8–20 yuan |

**Fallback wiring (only when using a 3.3V module):**

```
USB-TTL(3.3V) ──► TXS0102(VCCA=1.8 from near the phone? or the module’s own 1.8)
Note: it is still simpler to just buy an FT232 with a 1.8V setting.
```

Do not recommend beginners stealing 1.8V from a board LDO for the shifter; prefer a multi-level FT232.

---

### 3.4 Minimal “just order this” cart (copy-friendly)

| # | Product (search terms) | Qty | About |
|---|---------------|------|------|
| 1 | `FT232RL 1.8V 3.3V 5V USB转TTL` | 1 | ¥20–40 |
| 2 | `杜邦线 母对母` | 1 strip | ¥5 |
| 3 | `漆包线 0.1mm` or `30AWG 电子线 彩色` | 1 | ¥8 |
| 4 | `恒温电烙铁` (skip if you already have one) | 1 | ¥50 |
| 5 | `焊锡丝 0.5mm` + `助焊膏` | 1 each | ¥15 |
| 6 | `数字万用表` (skip if you already have one) | 1 | ¥30 |
| 7 | `手机拆机工具套装` | 1 | ¥15 |

**Total about: ¥80–160** (under ¥40 if you already have an iron / multimeter).

---

### 3.5 Order checklist (tick before paying)

- [ ] The listing title or details clearly say **1.8V**
- [ ] The chip is an FT232 series (or another bridge that clearly supports 1.8V IO)
- [ ] Jumpers / DIP switches are present; the manual explains how to select 1.8V
- [ ] Dupont wires are included or bought separately
- [ ] You did **not** settle for a CH340 “generic 3.3V/5V” adapter

First thing after it arrives: plug it into the PC, set the jumper/DIP to **1.8V**, and measure the module `VCC` pin with a multimeter (on some boards VCC follows the level setting). Only then touch the phone.

---

## 4. Host software setup

### 4.1 Linux (recommended; matches this project)

```bash
# Install terminal tools
sudo apt update
sudo apt install -y screen picocom minicom

# After inserting the module, list device nodes
ls -l /dev/ttyUSB* /dev/ttyACM*

# Add the current user to dialout (once; re-login to take effect)
sudo usermod -aG dialout "$USER"

# Open the serial port (pick one)
sudo screen /dev/ttyUSB0 115200
# or
sudo picocom -b 115200 /dev/ttyUSB0
```

Exit:

- `screen`: `Ctrl+A` then `K`, then confirm `Y`
- `picocom`: `Ctrl+A` then `Ctrl+X`

### 4.2 Windows (optional)

1. Install the FTDI VCP driver (FT232 sellers usually provide a link)  
2. Note the COM port in Device Manager  
3. Use [PuTTY](https://www.putty.org/): Connection type = Serial, Speed = 115200

### 4.3 Module self-test (not connected to the phone)

1. Remove any factory TX↔RX shorting jumper on the module (if present)  
2. Short the module’s own **TXD–RXD** with a Dupont wire  
3. Open the serial terminal; typed characters should echo back → module is OK  
4. Then rewire to the phone

---

## 5. Hands-on steps (detailed)

### Step 1: Power off and open the phone

1. Power off and unplug the charger.  
2. Heat the back-cover edges and separate slowly with a plastic pick (the glass breaks easily; go slowly).  
3. **Disconnect the battery flex first** (see teardown photos such as `ginkgo-battery-connector.jpg`), then touch the motherboard.

### Step 2: Locate the test points

1. Open the LLDM516 boardview / PCB drawing and search `TP0003`, `TP0012`.  
2. Match silkscreen on the board; find a reliable nearby GND (the metal ring of a screw hole usually works).  
3. Confirm with a multimeter: pads are not shorted to ground with power off; after power-up TX≈1.8V.

> If you do not have a boardview yet: you can first solder only **suspected DBG test points + GND**, power on, and listen on serial; try another pad if there is no output. **Do not** poke a 3.3V TX around at random.

### Step 3: Solder flying leads

1. Iron about 300–330℃, with flux.  
2. Put a tiny amount of solder on the TP → tin the stripped enamel wire → tack-solder.  
3. Solder the other end to a Dupont pin, or twist it onto a Dupont wire.  
4. Fix with high-temp tape so pulling does not rip the pad off.  
5. Sibling-device reference photos: `lavender-smallfull.jpg` / `lavender-smallpins.jpg`.

### Step 4: Connect the USB-TTL

1. Set the module level to **1.8V**.  
2. Cross-connect TX/RX per section 2.2 and attach GND.  
3. **Do not connect VCC to the phone**.  
4. Plug USB into the PC and open a `115200` terminal.

### Step 5: Power on and watch

1. Reconnect the battery and long-press power.  
2. You should first see **bootloader / XBL/ABL**-style logs.  
3. After flashing mainline with `console=` / `earlycon=`, kernel logs should appear through login.  
4. Downstream Android kernels may emit almost no UART (lavender has the same behavior); **this project treats mainline as the reference**.

### Step 6: Troubleshooting

| Symptom | Likely cause | Action |
|------|----------|------|
| No output at all | TX/RX swapped | Swap the two signal wires once |
| No output at all | Level is not 1.8V | Check the jumper; measure module and phone TX |
| Garbage | Wrong baud rate | Stay at 115200 |
| Garbage / intermittent | Poor contact, GND dry joint | Re-solder GND |
| RX LED blinks but no text | Level too low; ignored by a 3.3V module | Switch to a 1.8V module (a lavender pitfall) |
| Phone misbehaves after soldering | Pad short / level too high | Power off immediately, check for shorts, confirm 3.3V is not connected |

---

## 6. Matching this project’s cmdline

Ensure flash / boot.img contains:

```
console=ttyMSM0,115200n8
earlycon=msm_serial_dm,0x4a90000
```

In the mainline DTS, `&uart4` should be `okay` (the relevant ginkgo patch landed from 2026-01).

Acceptance:

```text
Serial shows the Linux version banner / earlycon output
You get a shell, or at least the raw panic text
```

---

## 7. Local image index

| File | Purpose |
|------|------|
| `ginkgo-usb-ttl-wiring.png` | Wiring diagram for this setup |
| `ginkgo-edl-testpoints.jpg` | Close-up of the board (EDL, not UART) |
| `willow-edl-testpoint.jpg` | Note 8T ISP/EDL labels |
| `ginkgo-battery-connector.jpg` | Tear-down / battery flex |
| `lavender-smallfull.jpg` etc. | Note 7 UART soldering + FT232 hands-on reference |

---

## 8. References

- Schematic: [Scribd Redmi Note 8 Schematic](https://www.scribd.com/document/461718408/Schematic-Redmi-Note-8-pdf)  
- Schematic download: [Elektrotanya LLDM516](https://elektrotanya.com/xiaomi_phone_redmi_note_8_lldm516_schematics.pdf/download.html)  
- Mainline UART patch: [Enable debug UART on ginkgo](https://www.spinics.net/lists/kernel/msg6011109.html)  
- Sibling-device experience: [UART on Redmi Note 7](https://wantguns.dev/blog/uart-on-lavender/)  
- FT232 multi-level notes example: [Diustou FT232 Multi-Level](https://wiki.diustou.com/cn/FT232_Multi-Level_USB_TTL)

---

## 9. Todo (on-device)

- [ ] Obtain the LLDM516 boardview and screenshot-label TP0003 / TP0012  
- [ ] Confirm this unit’s TX idle level ≈ 1.8V with a multimeter  
- [ ] After soldering, archive a boot serial log under `backup/ginkgo/logs/`
