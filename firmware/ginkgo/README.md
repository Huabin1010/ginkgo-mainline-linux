# ginkgo firmware

**Language:** English | [简体中文](README.zh-CN.md)

Extracted from a Redmi Note 8 (ginkgo) for mainline Linux. Extraction date: 2026-08-04. Needs `adb root`.

## Wi-Fi (WCN3990)

On-device path: `/vendor/firmware_mnt/image/`

| File | Size | Source | Mainline install path |
|------|------|--------|------------------------|
| `wifi/wlanmdsp.mbn` | 3,720,220 B | **device** | `/lib/firmware/ath10k/WCN3990/hw1.0/wlanmdsp.mbn` |
| `wifi/bdwlan.bin` | 26,328 B | **device** | Generic board; **do not** use as the primary BDF on ginkgo |
| `wifi/bdf_c3j.bin` | 26,328 B | **device** | Copy **as-is** to `board.bin` (do not wrap as `board-2.bin`) |
| `wifi/bdf_c3x.bin` | 26,328 B | **device** | Spare board calibration |
| `wifi/data.msc` | 592,741 B | **device** | MSC calibration |
| `wifi/firmware-5.bin` | 60 B | **linux-firmware** | `/lib/firmware/ath10k/WCN3990/hw1.0/firmware-5.bin` |

### firmware-5.bin

**The phone does not ship `firmware-5.bin`.** Android CNSS loads `wlanmdsp.mbn` directly.

Mainline ath10k also wants a 60-byte `firmware-5.bin` descriptor (feature bits / metadata). It was downloaded from [linux-firmware](https://gitlab.com/kernel-firmware/linux-firmware/-/tree/main/ath10k/WCN3990/hw1.0).

### Version match

| Source | wlanmdsp version |
|--------|------------------|
| **This device** | `WLAN.HL.3.0.2-00656` (FW:3.0.2.0.656.1) |
| linux-firmware default | `WLAN.HL.2.0-01387` (older, **do not mix**) |

**Use the device `wlanmdsp.mbn`** with a matching `firmware-5.bin`. If ath10k probe fails, you may need a descriptor built for 3.0.2-656.

**`board.bin` (verified 2026-08-18):** copy `bdf_c3j.bin` **as-is** to `/lib/firmware/ath10k/WCN3990/hw1.0/board.bin`. Do not treat a raw BDF as `board-2.bin` (that file needs QCA-ATH10K-BOARD IE wrapping; ath10k then misses qmi-board-id). `scripts/configure-rootfs.sh` already installs it this way.

Full write-up: [docs/ginkgo-wifi-complete-2026-08-18.md](../../docs/ginkgo-wifi-complete-2026-08-18.md).

## GPU (Adreno 610)

| File | Size | Source | Mainline install path |
|------|------|--------|------------------------|
| `gpu/a610_zap.mdt` + `.b00/.b01/.b02` | device-signed | **this phone** `/vendor/firmware/` | `/lib/firmware/qcom/sm6125/xiaomi/ginkgo/` |
| `gpu/a610_zap.elf` | ELF for comparison | **this phone** | Do not install; MDT is loaded in segments |
| `gpu/a630_sqe.fw` | 34188 B | **linux-firmware** | `/lib/firmware/qcom/a630_sqe.fw` |
| `gpu/a630_sqe.fw.vendor` | 31980 B | **this phone** | **Do not use**; version `0x187`, mainline wants `>= 0x190` |

Zap must be the device-signed image, not the generic linux-firmware `a610_zap`. SQE does not go through TZ PAS; use linux-firmware `0x207` so `a6xx_ucode_check_version()` is happy.

Xiaomi moved GPU PIL reserved memory to `0x57515000` (SoC default is `0x57115000`). Full write-up: [docs/ginkgo-gpu-desktop-2026-08-19.md](../../docs/ginkgo-gpu-desktop-2026-08-19.md).

## Touch (Novatek NT36672A SPI)

| File | Size | Use |
|------|------|-----|
| `novatek_ts_tianma_fw.bin` | 118,784 B | Tianma panel touch firmware (this phone) |
| `novatek_ts_tianma_mp.bin` | 118,784 B | Tianma MP test firmware |
| `novatek_ts_ebbg_fw.bin` | 118,784 B | EBBG panel variant |
| `novatek_ts_ebbg_mp.bin` | 118,784 B | EBBG MP test firmware |

## Reference copies

`wifi/` also has linux-firmware upstream copies (`*.linux-firmware`) and a qcm2290 variant for comparison. **Do not mix them with the device files.**
