**Language:** English | [简体中文](zh-CN/ginkgo-wifi-complete-2026-08-18.md)

# Redmi Note 8 (ginkgo) mainline WiFi: from “no wlan0” to 5 GHz VHT80 association with a displayed rate

> Device: Xiaomi Redmi Note 8 · codename **ginkgo** · SoC **SM6125 (trinket)** · serial `<serial>`  
> Chip: Qualcomm **WCN3990** · bus **SNOC** `wifi@c800000` · driver **`ath10k_snoc`** (not `wcn36xx`, not downstream `icnss`)  
> Firmware: **this unit’s** `wlanmdsp.mbn` = `WLAN.HL.3.0.2-00656` (**do not** mix with linux-firmware HL.2.0)  
> BDF: `bdf_c3j.bin` copied as-is to `board.bin` (26328 B), **not** a `board-2.bin` IE wrapper  
> Antenna: firmware `num_rf_chains=1` (1x1); HT MCS 0–7; VHT NSS1 MCS 0–9  
> QMI: `chip_id 0x120`, `board_id 0xff`  
> Acceptance: 2026-08-18 · associated to `<test-ap-5g>` (ch36 / 5180 / VHT80), IP `<wlan0-dhcp>`, rate **292.5 MBit/s VHT-MCS 7 80MHz**  
> Prerequisite: display P3 and touch P4 already work on this unit. Do not change pinmux / CS / `CONFIG_FB` / display prefetch to “fix WiFi”.

**Related docs / skills**

| Document | Content |
|----------|---------|
| [ginkgo-display-complete-2026-08-17.md](./ginkgo-display-complete-2026-08-17.md) | Display image (prerequisite on this unit) |
| [ginkgo-touch-complete-2026-08-17.md](./ginkgo-touch-complete-2026-08-17.md) | Touch points (prerequisite on this unit) |
| [ginkgo-mainline-bringup-chronicle.md](./ginkgo-mainline-bringup-chronicle.md) | Full-device bring-up timeline |
| [firmware/ginkgo/README.md](../firmware/ginkgo/README.md) | wlanmdsp / BDF extracted from this unit |
| [usb-connect.sh](../scripts/usb-connect.sh) | Reconfigure RNDIS after every reboot |
| [reboot-fastboot.sh](../scripts/reboot-fastboot.sh) | Enter fastboot from Ubuntu |

Verify with `fastboot boot out/boot.img`, **do not** `fastboot flash boot`.

---

## 0. One-sentence conclusion

wlan0 finally scanned, associated, reached the network, and displayed a VHT rate not because we “waited a bit longer for QMI” or “swapped in a linux-firmware blob”, but because of **six stacked facts**:

1. **The WLAN DSP lives in MPSS.** Without `rmtfs` + `remoteproc_mpss` starting, QRTR never has WLFW, and ath10k stays forever at “waiting for firmware”.  
2. **MSA is 1 MB, and it must be hyp_assign’d.** Downstream hands icnss `qcom,wlan-msa-memory = <0x100000>`; if ath10k registers 2 MB from reserved-memory, firmware walks off the TZ mapping. `qcom,msa-fixed-perm` skips assign; about 11 ms after CAL the whole MPSS dies.  
3. **BDF is this unit’s `bdf_c3j.bin` used as `board.bin`.** Do not treat raw BDF as `board-2.bin`, and do not mix in HL.2.0 `wlanmdsp.mbn`.  
4. **Empty CAL must answer `CAL_DOWNLOAD` first, then send `CAL_REPORT`.** `cal_done=1` makes firmware wait forever; an empty report without answering download triggers cold-boot RF calibration, then `SFR Init: wdog` about 11 ms later. Factory NV is in modemst (rmtfs); the host-side cal file is empty. That is the protocol, not a “missing file”.  
5. **STA must create the BSS peer (the AP’s BSSID) before `vdev-start`.** mac80211 does `assign_vif_chanctx` first, then `sta_state`. The original mainline order made WLAN.HL.3.x return `vdev-start status 0xffffffff`. **Adding a STA self-peer is wrong** — it collides with the implicit self-peer from `vdev_create`, firmware asserts in `wlan_vdev_free`, and the modem crash-loops.  
6. **Rate display cannot rely on minstrel / HTT PEER_STATS / WMI PEER_STATS_INFO.** Firmware RC; SNOC TLV HTT has no PEER_STATS message; `REQUEST_PEER_STATS_INFO` times out after 3 seconds. The correct approach: remember the MCS of the last HT/VHT **data frame**; do not let a 6 Mbps beacon overwrite it.

Along the way we also hit: thermal quiet mode crashing firmware, WMI_INIT advertising chainmask `0x7`, 11g channels wrongly marked HT/VHT, the channel list rewritten as HT/VHT80 (`halphy_caldb_find_rfsub` crash), and wifi-setup bouncing wlan0 to change the MAC.

---

## 1. Lessons (for the next WCN3990 / HL.3.x bring-up)

### 1.1 Split the link into independently falsifiable stages

```
MPSS up  ≠  WLFW QMI handshake  ≠  BDF/CAL done  ≠  FW_READY/wlan0
  ≠  scan sees APs  ≠  vdev-start succeeds  ≠  associated  ≠  has IP  ≠  rate display correct
```

| Stage | False positive | Conclusive check |
|-------|----------------|------------------|
| Driver probe | `ath10k_snoc` bound, four regulators enabled | WLFW present on QRTR |
| QMI | `qmi BDF download done` | **no** following `fatal error: SFR Init`, and `FW_READY` appears |
| wlan0 | interface exists | `iw phy` has 2.4 + 5 GHz, `iw scan` sees the target AP |
| Association | NetworkManager “Connecting” | `wlan0: associated` + `status=0` + DHCP |
| Rate | just-associated `rx bitrate: 6.0` | after traffic **VHT-MCS + 80MHz**, idle does not fall back to 6.0; `tx bitrate` has a field |

The most valuable falsification this time: **every QMI step succeeded, then MPSS died 11 ms later** — the problem was not “firmware not found”, but **host CAL/MSA semantics not matching downstream icnss**.

### 1.2 WLAN.HL.3.x is not HL.2.0 plus two TLVs

Mainline ath10k’s WCN3990 path was written for **HL.2.0 / linux-firmware**. ginkgo ships **HL.3.0.2**. Differences show up as “status -1 / 0xffffffff / the whole modem watchdog”, not `-EINVAL`.

HL.3.x semantics already matched and must not be reverted:

| Item | HL.2.0 mainline default | ginkgo HL.3.0.2 requirement |
|------|-------------------------|-----------------------------|
| `wlanmdsp.mbn` | linux-firmware `WLAN.HL.2.0-01387` | **this unit’s** `WLAN.HL.3.0.2-00656` |
| WMI_INIT chainmask | hardcoded `0x7` | firmware’s real chain count (this unit `0x1`) |
| `vdev_create` | stops at MAC | `num_cfg_txrx_streams` + 2G/5G `VDEV_TXRX_STREAMS` TLV |
| `vdev_start` | stops at `disable_hw_ack` | `preferred_tx/rx_streams`; may add `LDPC_RX` |
| STA vdev-start | start as soon as chanctx is assigned | can start only after **BSS peer exists** |
| beacon/dtim | 0 allowed | 0 is rejected; default 100 / 1 |
| Channel flags | 11g may also carry HT/VHT | HALPHY aligns to phymode; 11g must not ALLOW_HT/VHT |
| THERM_THROT quiet | send if firmware advertises it | **sending it crashes** (PC ~`0xb0006504`) |

### 1.3 “self-peer” and “BSS peer” are not the same thing

ath11k / qcacld STA path: first add the **AP’s BSSID** as BSS peer, then vdev-start.  
`vdev_create` already created a **self-peer for the STA’s own MAC** in firmware. Host then `peer_create(own MAC)` collides with that implicit peer → `wlan_vdev_free` assert → MPSS crash-loop.

mac80211 order is:

```
assign_vif_chanctx  →  (mainline originally vdev-start here)  →  sta_state then peer_create(BSSID)
```

So for WCN3990 non-AP / non-monitor: **chanctx only records the chandef and delays start; start after `sta_state` has created the BSS peer.** Do not invent a third path that adds a self-peer.

### 1.4 Antenna count comes from service-ready; do not edit hw_params to “look like 2x2”

`iw phy`: TX/RX antenna `0x1`. `wmi service ready chains 1`. ginkgo is single-antenna; tying CH1 supply to CH0 only keeps `ath10k_snoc` bulk-get from failing.  
Writing WMI_INIT / hw_params as `0x3` or `0x7` makes HALPHY blow up in vdev-start or caldb.

### 1.5 Rate: 6 Mbps is often real — just not the frame you wanted to see

A 5 GHz beacon is 6 Mbps OFDM. mac80211 `rx bitrate` is the **last RX**. When idle, the last frame is almost always a beacon.  
After ping/traffic the data is VHT80 MCS7 = 292.5 Mbps (1x1 long GI). That is the link rate.

TX has no minstrel samples (`HAS_RATE_CONTROL` + firmware RC). SNOC TLV HTT table has **no** `PEER_STATS`. `WMI PEER_STATS_INFO` requests time out on this HL.3.0.2. So the driver must cache the last data MCS itself.

### 1.6 The debug channel is part of bring-up

- After every `fastboot boot` / reboot, first run `./scripts/usb-connect.sh`  
- SSH: `root@192.168.7.2`, password `$GINKGO_ROOT_PASSWORD`, host `192.168.7.1`  
- Enter fastboot: `./scripts/reboot-fastboot.sh`  
- Verify: `fastboot boot out/boot.img`, **do not flash**  
- The first `fastboot boot` often fails; **do not usbreset** (you lose USB). If stuck, `killall -9 fastboot`  
- `pkill -f 'fastboot boot'` will kill the current shell by mistake  
- After MPSS dies, ping may still work and sshd may be dead: have a human key into fastboot; do not `echo start` a rproc that is already running

### 1.7 Change one variable at a time

The most common WiFi failed experiments stacked two layers: MSA size + `msa-fixed-perm`, self-peer + delayed start, chainmask + channel-list HT/VHT80. Reproduce a change with a standalone `fastboot boot` first, then stack the next item.

---

## 2. Goals, acceptance, non-goals

### 2.1 Goal

On mainline Linux, walk the full path:

```
MPSS → WLFW QMI → ath10k_snoc → mac80211 → wlan0 → 2.4G and 5G association → IPv4
```

and make `iw` / NetworkManager **display the data MCS**, not forever 6 Mbps with no `tx bitrate`.

### 2.2 Quantitative acceptance (measured 2026-08-18)

| Metric | Success threshold | Measured |
|--------|-------------------|----------|
| `wlan0` | UP | UP |
| Regulatory domain | `iw reg get` is CN | `ginkgo-wifi-setup.sh` only `iw reg reload` + `iw reg set CN` |
| Scan | sees `<test-ap-2g>` and `<test-ap-5g>` | yes |
| Association | `associated` status=0 | `<test-ap-5g>` aid=7 |
| Channel | 5 GHz VHT80 | freq 5180, width 80 MHz, center1 5210 |
| IP | DHCP | `<wlan0-dhcp>/24` gw `192.168.1.1` |
| ping | gateway reachable | ~15 ms, 6/6 |
| Rate (after traffic) | VHT NSS1 80 MHz | **RX/TX 292.5 MBit/s VHT-MCS 7 80MHz VHT-NSS 1** |
| Idle | does not fall back to 6.0 | still 292.5 after 3–4 s |

### 2.3 Non-goals / do not do

- Do not `fastboot flash boot` / write userdata  
- Do not add STA self-peer back, rewrite the channel list as HT/VHT80, or set hw_params chain mask `0x3`  
- Do not add `qcom,msa-fixed-perm`, `qcom,use-guard-pages`, `qcom,no-msa-ready-indicator`  
- Do not `echo start` a running rproc  
- Do not treat raw BDF as `board-2.bin`  
- Do not mix linux-firmware `WLAN.HL.2.0` `wlanmdsp.mbn`  
- Do not restore GNOME; do not bounce wlan0 / change MAC to “fix WiFi”  
- Do not revert display prefetch, touch CS, or `#undef CONFIG_FB` in `nt36xxx.h` for WiFi

---

## 3. Hardware and software baseline

### 3.1 Chip and supplies

| Item | Value |
|------|-------|
| Chip | WCN3990 |
| Registers | `0x0c800000`, 8 MB |
| CE interrupts | GIC SPI **358–369** (downstream `icnss@C800000` CE0–CE11) |
| SMMU SID | **`0x80`** (sm6115 is `0x1a0`; copying the wrong one causes an IOMMU fault) |
| MSA | `0x53300000`, **1 MB**; the next 1 MB is `wlan_msa_guard` (overrun guard, not a second MSA) |
| CX/MX | L8A `vdd-0.8-cx-mx` |
| XO | L16A `vdd-1.8-xo` |
| RFA | L17A `vdd-1.3-rfa` |
| CH0 / CH1 | both on L23A (single antenna; driver still bulk-gets ch1) |

### 3.2 Firmware files

Path: `/lib/firmware/ath10k/WCN3990/hw1.0/`  
Source: `firmware/ginkgo/wifi/`, installed into rootfs by `scripts/configure-rootfs.sh`.

| File | Size | Notes |
|------|------|-------|
| `wlanmdsp.mbn` | 3,720,220 B | **this unit’s** HL.3.0.2-00656. MPSS pulls it via tqftpserv |
| `firmware-5.bin` | 60 B | ath10k feature-bit descriptor. Android CNSS **does not need** it; mainline does. May come from the 60-byte linux-firmware file; **do not** also swap wlanmdsp |
| `board.bin` | 26,328 B | **`bdf_c3j.bin` copied as-is**. ath10k falls back to it when `board-2.bin` does not match qmi-board-id |
| `board-2.bin` | — | **do not install raw BDF**. That format is QCA-ATH10K-BOARD IE |

`firmware-5.bin` is not the MAC firmware body. MAC/PHY live in `wlanmdsp.mbn`.

### 3.3 Userspace

| Component | Role |
|-----------|------|
| `ginkgo-mpss.service` → `ginkgo-start-mpss.sh` | `echo start` to `6080000.remoteproc` (PAS `auto_boot` is false) |
| `rmtfs` | exposes modemst as `/dev/qcom_rmtfs_mem1`; factory WLAN NV lives here |
| `tqftpserv` + `pd-mapper` | serve `wlanmdsp.mbn` / `.mdt` to MPSS |
| `ginkgo-wifi-setup.service` | **only** `iw reg reload` + `iw reg set CN`. Link down/up forbidden |

Kernel config: `CONFIG_ATH10K=y`, `CONFIG_ATH10K_SNOC=y` in `config/ginkgo.fragment`. **Do not** enable `CONFIG_WCN36XX` (that is Prima WCN3660/3680).

ath10k SNOC HTT is **LL** (`ATH10K_DEV_TYPE_LL`): data RX walks descriptors, `ath10k_htt_rx_h_rates` fills VHT encoding. USB/SDIO HL RX indications often omit the rate; that is a different path.

---

## 4. Layered debug (this order; do not skip)

```
L6 user-visible   iw link has VHT rate + ping works
L5 mac80211       associated + DHCP
L4 WMI            vdev-start status=0, BSS peer before start
L3 ath10k         wlan0 + iw phy dual-band
L2 QMI            BDF + CAL_DOWNLOAD reply + CAL_REPORT + FW_READY, MPSS stays up
L1 MPSS           remoteproc running, QRTR has WLFW
L0 electrical/DT  wifi@c800000, SID 0x80, MSA 1MB, four regulators
```

| Symptom | Check first |
|---------|-------------|
| No `wlan0`, dmesg has no WLFW | rmtfs node, `ginkgo-mpss`, `cat remoteproc0/state` |
| Crash ~50 ms after QMI reaches CAP | is MSA still 2 MB |
| `SFR Init: wdog` ~11 ms after BDF/CAL done | was `msa-fixed-perm` added, or empty CAL not answering `CAL_DOWNLOAD` |
| Stuck waiting for FW_READY | is `cal_done=1` or CAL_REPORT never sent |
| `vdev-start` `0xffffffff` / status -1 | is there a BSS peer; is INIT chainmask `0x7`; is 11g wrongly marked HT/VHT |
| Modem crash-loop on associate | was STA self-peer added again, or channel list changed to HT/VHT80 |
| Only 6 Mbps, no `tx bitrate` | ping first then look; idle 6.0 is a beacon. Driver should cache data MCS |
| `iw station dump` hangs 3 seconds | do not send `REQUEST_PEER_STATS_INFO` to this firmware |

---

## 5. Failed-experiment timeline (so we do not walk it again)

In chronological order, not “what looked reasonable”.

### 5.1 DT + this-unit firmware only: `ath10k_snoc` bound, no wlan0

SM6125 originally had no `wifi@c800000` (sm6115 does). After adding the node, ginkgo supplies, and `board.bin=c3j`, the driver bound and all four rails came up. QRTR had **no WLFW node at all**.

Root cause: WCN3990 WLAN firmware is brought up by **MPSS**. PAS defaults to `auto_boot=false`. Without rmtfs, Hexagon fatals about 40 s after boot, and WLFW never appears.

### 5.2 Start MPSS without rmtfs: crash after ~40 s

`&remoteproc_mpss` + `ginkgo-start-mpss.sh` is not enough. ginkgo DTS must keep:

```
rmtfs_mem@89b01000  2 MB
qcom,client-id = <1>
qcom,vmid = MSS_MSA + NAV
```

The address follows sm6115 mainline convention; it is not an arbitrary free DRAM hole.

### 5.3 MSA advertised as 2 MB: crash ~50 ms after CAP

Downstream icnss only hands **1 MB** to WLAN. ath10k uses the full length of `memory-region`. When reserved-memory is 2 MB, WLAN walks off the TZ MSA mapping.

Fix: `wlan_msa_mem` = `0x53300000` / 1 MB; the following 1 MB is `wlan_msa_guard` placeholder, **not** given to the wifi node.

### 5.4 `qcom,msa-fixed-perm`: crash ~11 ms after CAL

Mainline this property skips MSA hyp_assign. Downstream `qcom,wlan-msa-fixed-region` **only pins the PA**; icnss still assigns to `MSS_MSA + WLAN + WLAN_CE`. Copying the property name is not copying the semantics.

Log shape:

```
qmi BDF download done
qmi cal report done
qcom_q6v5_pas: fatal error: SFR Init: wdog or kernel error suspected
```

**Do not** add `qcom,msa-fixed-perm`.

### 5.5 `qcom,no-msa-ready-indicator`

This firmware **does** send `MSA_READY_IND`. Adding the property only moves the crash earlier; the root cause is unchanged.

### 5.6 `cal_done=1`: never reach FW_READY

Intent: “factory cal is already in EFS, skip cold cal”. Actual HL.3.0.2 behavior: `cal_done=1` **suppresses** `INITIATE_CAL_DOWNLOAD_IND`, then firmware waits forever for a `CAL_REPORT` it believes should arrive.

Correct combination:

- host cap: `bdf_support=1`, `cal_filesys_support=1`, `cal_done=0`  
- For each `CAL_DOWNLOAD_IND`, reply with an **empty** download (`total_size=0`, `end=1`)  
- After the burst, delayed work sends `CAL_REPORT`  
- Factory NV still comes from rmtfs/modemst; no host-side cal file is needed

Empty report **without** first answering download: firmware takes cold-boot RF calibration, then watchdog ~11 ms later. Never sending report: stuck at FW_READY.

### 5.7 Mixing linux-firmware `WLAN.HL.2.0` `wlanmdsp.mbn`

Not the same set as the 60-byte `firmware-5.bin`. Must use this unit’s 3.0.2-00656. `firmware-5.bin` may be that 60-byte linux-firmware file (feature bits); **wlanmdsp may not**.

### 5.8 Raw BDF as `board-2.bin`

`board-2.bin` needs QCA-ATH10K-BOARD IE. Stuffing 26328 bytes of raw data in makes ath10k fail to match board-id. Correct: `cp bdf_c3j.bin board.bin`; do not install `board-2.bin` (or only install a truly converted IE file).

`bdwlan.bin` is the generic board; ginkgo needs the **c3j** variant to have full dual-band calibration.

### 5.9 Thermal quiet mode

Firmware advertises `THERM_THROT`. Mainline then sends `pdev_set_quiet_mode`. HL.3.x crashes in `wlan_process` (HL.3.0.2 PC ~`0xb0006504`, HL.3.2 ~`0xb0008e20`). On WCN3990 **just return; do not send**.

### 5.10 WMI_INIT chainmask `0x7` → vdev-start status -1

Mainline TLV INIT hardcodes `tx/rx_chain_mask = 0x7` (3x3). service-ready says chains=1. HALPHY uses that INIT capability at vdev-start and returns -1.  
Fix: WCN3990 sends a mask from `num_rf_chains` (this unit `0x1`). **Do not** change `hw_params` `0x7` to pretend 2x2.

### 5.11 11g channels with ALLOW_HT/VHT

Mainline once marked HT/VHT regardless of phymode. HALPHY refuses those flags on 11g/11a. Flags must follow `chan_to_phymode()`.

STA vdev-starts before `BSS_CHANGED_BEACON_INT`: `bcn_intval/dtim` of 0 is also rejected by HL.3.x. Default 100 / 1.

### 5.12 STA self-peer: modem crash-loop

To satisfy “must have a peer before start”, we once `peer_create(STA’s own MAC)`. Firmware already has a self-peer from `vdev_create`. Collision → `wlan_vdev_free` assert → MPSS fatals repeatedly. **Must revert; never add it back.**

Correct: delay vdev-start until the BSS peer (BSSID) is created.

### 5.13 Channel list rewritten as HT/VHT80: `halphy_caldb_find_rfsub` crash

Wanted to “advertise 80 MHz capability”. Firmware caldb is built for 1x1 / this unit’s BDF; host rewriting the channel list walks off the table. Capability comes from service-ready (this unit already can VHT80 NSS1).

### 5.14 `ginkgo-wifi-setup.sh` bounce wlan0 / change MAC

Link down/up races with ath10k scan and crashes during HL.3.x bring-up. The script may only `iw reg reload` + `iw reg set CN`.

### 5.15 `WMI PEER_STATS_INFO` for TX rate: first `iw` hangs 3 seconds

`pdev_param peer_stats_info_enable=1` this firmware swallows (no warn). `REQUEST_PEER_STATS_INFO` **never returns an event**; `wait_for_completion_timeout` 3 s. After timeout this path must be turned off, or every dump hangs.  
TX rate switched to “last data RX MCS cache”. On 1x1, TX/RX MCS are the same order of magnitude — enough for `iw` / NM display.

---

## 6. Decisive fixes (code level)

### 6.1 DT: wifi node + 1 MB MSA + no msa-fixed-perm

`sm6125.dtsi`: `wifi@c800000`, `compatible = "qcom,wcn3990-wifi"`, CE interrupts 358–369, `iommus = <&apps_smmu 0x80 0x1>`, `memory-region = <&wlan_msa_mem>` (1 MB).

`sm6125-xiaomi-ginkgo.dts`: `&wifi` five supplies (ch1=ch0), `qcom,calibration-variant = "Xiaomi_ginkgo"`, `status = "okay"`. `rmtfs_mem@89b01000`. `&remoteproc_mpss` points at this unit’s `modem.mdt`.

### 6.2 QMI host cap + empty CAL protocol

`ath10k_qmi_host_cap_send_sync`:

- `bdf_support = 1`  
- `cal_filesys_support = 1`  
- `cal_done = 0`  

`CAL_DOWNLOAD_IND` → empty download → delayed `CAL_REPORT`.

### 6.3 WMI_INIT chainmask follows firmware chain count

`wmi-tlv.c` `gen_init`: WCN3990 builds the mask from `num_rf_chains`. On-device log:

```
wmi tlv init chainmask tx 0x1 rx 0x1 fw_chains 1
```

### 6.4 HL.3.x TLVs for vdev_create / vdev_start

- `vdev_create`: `num_cfg_txrx_streams=2` + 2G/5G `VDEV_TXRX_STREAMS`  
- `vdev_start`: full `wmi_tlv_vdev_start_cmd` (includes `preferred_tx/rx_streams`), NSS clamped to 1..2  
- flags: `WMI_VDEV_START_LDPC_RX_ENABLED (1<<3)` (if HT LDPC capability is set)  
- Channel ALLOW_HT/VHT/ht40plus follows phymode

### 6.5 Delay vdev-start until after BSS peer

`ath10k_wcn3990_delay_vdev_start()`: WCN3990 and not AP / not monitor.

- `assign_vif_chanctx`: if there is still no peer, only store `delayed_chandef`, return 0  
- After `peer_create(BSSID)` succeeds in `sta_state`: `ath10k_mac_finish_delayed_vdev_start()`  
- If still delayed, stop before deleting the peer  

**Forbidden:** `peer_create(vif->addr)`.

### 6.6 Skip quiet mode

`thermal.c`: if `QCA_REV_WCN3990(ar)`, do not send quiet.

### 6.7 Rate: cache the last HT/VHT data RX

`htt_rx.c` `ath10k_process_rx`: data + HT/VHT → `arsta->last_data_rxrate`.  
`mac.c` `ath10k_sta_statistics`: do not return immediately when `!peer_stats_enabled`; fill RX from this cache, and if TX is still legacy/empty use the same cache. Beacon 6 Mbps no longer overwrites VHT.

Do not enable `supports_peer_stats_info` for WCN3990 (3 s timeout).

---

## 7. Acceptance logs

### 7.1 QMI / firmware (healthy)

```
ath10k_snoc c800000.wifi: wmi service ready chains 1 ht 0x381b vht 0x739011b2
wmi tlv init chainmask tx 0x1 rx 0x1 fw_chains 1
```

QMI: `chip_id 0x120`, `board_id 0xff`. BDF 26328 bytes. After CAL download/report, **FW_READY**, with no immediately following `SFR Init: wdog`.

### 7.2 Association to `<test-ap-5g>`

```
wlan0: authenticated
wlan0: RX AssocResp ... status=0 aid=7
wlan0: associated
Device wlan0 successfully activated
SSID: <test-ap-5g>  freq 5180  width 80 MHz  center1 5210
IP: <wlan0-dhcp>/24  gw 192.168.1.1
```

Delayed-start log:

```
wcn3990 defer vdev 0 start until BSS peer
wcn3990 delayed vdev 0 start after BSS peer
```

### 7.3 Rate (after pinging the gateway)

```
rx bitrate: 292.5 MBit/s VHT-MCS 7 80MHz VHT-NSS 1
tx bitrate: 292.5 MBit/s VHT-MCS 7 80MHz VHT-NSS 1
```

Just associated, before any data, you may still see 6.0 (beacon). After ping or opening a page it should rise to VHT and stay there when idle. `dmesg` must **not** show `timed out waiting peer stats info`.

292.5 Mbps = 1x1 VHT80 MCS7 long GI (table entry 2925×100 kbps). SGI would be 325.0. This is near the correct ceiling for a single antenna, not “one antenna missing” as a bug.

---

## 8. Key files

| Item | Path |
|------|------|
| SoC wifi node / MSA | `linux/arch/arm64/boot/dts/qcom/sm6125.dtsi` |
| ginkgo supplies / rmtfs / mpss | `linux/arch/arm64/boot/dts/qcom/sm6125-xiaomi-ginkgo.dts` |
| Delayed vdev-start, channel flags, sta_statistics | `linux/drivers/net/wireless/ath/ath10k/mac.c` |
| `vdev_start_delayed` | `linux/drivers/net/wireless/ath/ath10k/core.h` |
| INIT chainmask, vdev TLV | `linux/drivers/net/wireless/ath/ath10k/wmi-tlv.c` |
| HL.3.x structs / LDPC flag | `linux/drivers/net/wireless/ath/ath10k/wmi-tlv.h`, `wmi.h` |
| QMI host cap / CAL | `linux/drivers/net/wireless/ath/ath10k/qmi.c` |
| Skip quiet | `linux/drivers/net/wireless/ath/ath10k/thermal.c` |
| Data MCS cache | `linux/drivers/net/wireless/ath/ath10k/htt_rx.c` |
| Kernel fragment | `config/ginkgo.fragment` |
| Firmware install | `scripts/configure-rootfs.sh` |
| This-unit firmware | `firmware/ginkgo/wifi/` |
| Start MPSS | `rootfs-overlay/usr/local/sbin/ginkgo-start-mpss.sh` |
| Regulatory domain | `rootfs-overlay/usr/local/sbin/ginkgo-wifi-setup.sh` |

---

## 9. How to verify (regression)

```bash
source scripts/env.sh
./scripts/build-kernel.sh
./scripts/build-bootimg.sh
# 手机在 Ubuntu 且 SSH 好：
./scripts/reboot-fastboot.sh
fastboot boot out/boot.img
# 起来后：
./scripts/usb-connect.sh
./scripts/ssh-run.sh \
  'iw dev wlan0 link; ping -c 6 -W 2 192.168.1.1; iw dev wlan0 link; iw dev wlan0 station dump | grep bitrate'
```

Expect: the second `iw link` has both `rx bitrate` and `tx bitrate` VHT-MCS lines.

Test AP: `<test-ap-5g>` (5 GHz), 2.4G same passphrase `<test-ap-2g>`. Do not hardcode the password in scripts.

---

## 10. Contrast with display / touch bring-up

| | Display P3 | Touch P4 | WiFi P5 (this file) |
|--|------------|----------|---------------------|
| False positive | backlight on, DCS complete, INTF 60fps | event node present, chip ID present | QMI BDF/CAL success, wlan0 UP, rx 6.0 |
| Conclusive check | DSI TPG on screen; FIFO `0x1010` | `Update firmware success` + points | associated + ping + **VHT rate does not drop to 6.0** |
| Mainline default trap | prog fetch 24 vs VFP 10 | DT `spi-cs-high` vs MODE_0 | HL.2.0 WMI/QMI vs this unit’s HL.3.0.2 |
| Copying downstream must copy semantics | LCDB is not LAB/IBB | driver overrides CS, not the DT literal | `msa-fixed-region` ≠ `msa-fixed-perm` |
| After a crash | USB may still work | sshd often dies; human enters fastboot | MPSS crash-loop likewise needs a human in fastboot |

Display and touch already work. Fixing WiFi **must not** touch the already-verified root causes on those two trees.

---

## 11. Do-not-repeat list (for future self)

1. Do not add STA **self-peer** back  
2. Do not rewrite the channel list as HT/VHT80 to “enable 80 MHz”  
3. Do not change `hw_params` chain mask to `0x3`  
4. Do not `qcom,msa-fixed-perm` / `qcom,use-guard-pages` / `no-msa-ready-indicator`  
5. Do not MSA 2 MB  
6. Do not linux-firmware HL.2.0 `wlanmdsp.mbn`  
7. Do not raw BDF as `board-2.bin`  
8. Do not WCN3990 quiet mode  
9. Do not INIT chainmask `0x7`  
10. Do not bounce wlan0 / change MAC in wifi-setup  
11. Do not `REQUEST_PEER_STATS_INFO` on this firmware  
12. Do not `fastboot flash boot` unless the user explicitly says write the partition  
13. Do not usbreset to rescue `fastboot boot`  
14. Do not `echo start` a rproc that is already running

---

*End of record. WiFi acceptance date 2026-08-18.*
