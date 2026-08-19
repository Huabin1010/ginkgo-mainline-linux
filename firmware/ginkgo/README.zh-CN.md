**语言：** [English](README.md) | 简体中文

# ginkgo 固件文件

从 Redmi Note 8 (ginkgo) 设备提取，用于主线 Linux 适配。

提取日期：2026-08-04，设备序列号 <serial>（需 `adb root`）

## WiFi（WCN3990）

设备固件路径：`/vendor/firmware_mnt/image/`

| 文件 | 大小 | 来源 | 主线目标路径 |
|------|------|------|-------------|
| `wifi/wlanmdsp.mbn` | 3,720,220 B | **设备** | `/lib/firmware/ath10k/WCN3990/hw1.0/wlanmdsp.mbn` |
| `wifi/bdwlan.bin` | 26,328 B | **设备** | 通用板；ginkgo **不要**当主 BDF |
| `wifi/bdf_c3j.bin` | 26,328 B | **设备** | **原样**复制为 `board.bin`（不要包装成 `board-2.bin`） |
| `wifi/bdf_c3x.bin` | 26,328 B | **设备** | 备用板级校准 |
| `wifi/data.msc` | 592,741 B | **设备** | MSC 校准数据 |
| `wifi/firmware-5.bin` | 60 B | **linux-firmware** | `/lib/firmware/ath10k/WCN3990/hw1.0/firmware-5.bin` |

### 重要说明：firmware-5.bin

**设备上不存在 `firmware-5.bin`。** Android 的 CNSS 驱动直接加载 `wlanmdsp.mbn`，不需要这个文件。

主线 ath10k 额外需要一个 60 字节的 `firmware-5.bin` 描述符（特性位/metadata），已从 [linux-firmware](https://gitlab.com/kernel-firmware/linux-firmware/-/tree/main/ath10k/WCN3990/hw1.0) 下载。

### 版本匹配

| 来源 | wlanmdsp 版本 |
|------|--------------|
| **本机设备** | `WLAN.HL.3.0.2-00656`（FW:3.0.2.0.656.1） |
| linux-firmware 默认 | `WLAN.HL.2.0-01387`（较旧，**不可混用**） |

**必须使用设备版 `wlanmdsp.mbn`**，配合匹配的 `firmware-5.bin`。若 ath10k probe 失败，可能需要为 3.0.2-656 版本寻找/生成对应的 firmware-5.bin 描述符。

**`board.bin` 用法（2026-08-18 已验证）：** 把 `bdf_c3j.bin` **原样**复制为 `/lib/firmware/ath10k/WCN3990/hw1.0/board.bin`。不要把 raw BDF 当成 `board-2.bin`（那个文件需要 QCA-ATH10K-BOARD IE 包装，ath10k 对不上 qmi-board-id）。`scripts/configure-rootfs.sh` 已按此安装。

完整经历见 [docs/ginkgo-wifi-complete-2026-08-18.md](../../docs/ginkgo-wifi-complete-2026-08-18.md)。

## GPU（Adreno 610）

| 文件 | 大小 | 来源 | 主线目标路径 |
|------|------|------|-------------|
| `gpu/a610_zap.mdt` + `.b00/.b01/.b02` | 设备签名 | **本机** `/vendor/firmware/` | `/lib/firmware/qcom/sm6125/xiaomi/ginkgo/` |
| `gpu/a610_zap.elf` | 对照用 ELF | **本机** | 不安装；MDT 分段加载 |
| `gpu/a630_sqe.fw` | 34188 B | **linux-firmware** | `/lib/firmware/qcom/a630_sqe.fw` |
| `gpu/a630_sqe.fw.vendor` | 31980 B | **本机** | **不要用**；版本 `0x187`，主线要求 `>= 0x190` |

Zap 必须用本机签名，不能换 linux-firmware 的通用 `a610_zap`。SQE 不经 TZ PAS，用 linux-firmware 的 `0x207` 以满足 `a6xx_ucode_check_version()`。

Xiaomi 把 GPU PIL 保留内存改到 `0x57515000`（不是 SoC 默认 `0x57115000`）。完整经历见 [docs/ginkgo-gpu-desktop-2026-08-19.md](../../docs/ginkgo-gpu-desktop-2026-08-19.md)。

## 触控（Novatek NT36672A SPI）

| 文件 | 大小 | 用途 |
|------|------|------|
| `novatek_ts_tianma_fw.bin` | 118,784 B | Tianma 面板触控固件（本机） |
| `novatek_ts_tianma_mp.bin` | 118,784 B | Tianma MP 测试固件 |
| `novatek_ts_ebbg_fw.bin` | 118,784 B | EBBG 面板 variant |
| `novatek_ts_ebbg_mp.bin` | 118,784 B | EBBG MP 测试固件 |

## 参考文件

`wifi/` 子目录还包含 linux-firmware 上游版本（`* .linux-firmware`）和 qcm2290 variant，供版本对照，**不要与设备版混用**。
