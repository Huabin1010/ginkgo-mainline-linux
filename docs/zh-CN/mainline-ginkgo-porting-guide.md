**语言：** [English](../mainline-ginkgo-porting-guide.md) | 简体中文

# Redmi Note 8 (ginkgo) 主线 Linux 适配技术手册

> 文档版本：2026-08-04  
> 目标：**主线 Linux 7.0** → **Ubuntu 26.04 LTS** → **触控 + WiFi 可用**  
> 设备：Xiaomi Redmi Note 8 (`ginkgo`)，序列号 `<serial>`  
> 策略：**坚持主线，不采用下游内核**

---

## 目录

1. [项目目标与范围](#1-项目目标与范围)
2. [设备硬件清单（实测）](#2-设备硬件清单实测)
3. [存储与分区布局](#3-存储与分区布局)
4. [总线与地址映射](#4-总线与地址映射)
5. [主线内核现状与差距](#5-主线内核现状与差距)
6. [显示子系统（DRM/DSI）](#6-显示子系统drmdsi)
7. [触控子系统（SPI）](#7-触控子系统spi)
8. [WiFi 子系统（WCN3990/ICNSS）](#8-wifi-子系统wcn3990icnss)
9. [Reserved Memory 布局](#9-reserved-memory-布局)
10. [固件清单与提取](#10-固件清单与提取)
11. [主线 DTS 现状与待补全项](#11-主线-dts-现状与待补全项)
12. [参考移植案例](#12-参考移植案例)
13. [Ubuntu 26.04 集成方案](#13-ubuntu-2604-集成方案)
14. [Upstream Patch 清单](#14-upstream-patch-清单)
15. [分阶段实施计划](#15-分阶段实施计划)
16. [风险登记册](#16-风险登记册)
17. [附录：原始采集数据](#17-附录原始采集数据)

---

## 1. 项目目标与范围

### 1.1 明确目标

| 优先级 | 目标 | 验收标准 |
|--------|------|----------|
| P0 | 主线内核引导 | Linux 7.0 + ginkgo DTB 启动，serial console 可用 |
| P1 | Ubuntu 26.04 运行 | arm64 rootfs 挂载，systemd 启动，shell 登录 |
| P2 | DRM 显示 | 1080×2340 画面输出，`/dev/dri/card0` 存在 |
| P3 | 触控 | `/dev/input/event*` 有触摸事件，UI 可操作 |
| P4 | WiFi | `wlan0` 接口 up，可扫描并连接 AP |

### 1.2 明确不在范围内（初期）

- 蜂窝 Modem / 通话 / 短信
- 相机
- 音频播放
- 蓝牙
- 指纹
- GPU 3D 加速
- 电池电量精确读数

### 1.3 难度总评

| 综合 | **7.5 / 10** |
|------|-------------|
| 乐观工期 | 全职 3–4 个月 |
| 保守工期 | 6–9 个月 |
| 最大瓶颈 | WiFi（SM6125 SoC 主线 WiFi 节点缺失）+ 触控（SPI 驱动无主线） |

---

## 2. 设备硬件清单（实测）

采集时间：2026-08-04，来源：adb + dmesg + `/proc/device-tree`

### 2.1 SoC 与平台

| 项目 | 值 |
|------|-----|
| SoC | Qualcomm SM6125（内部代号 trinket） |
| 商业名 | Snapdragon 665 |
| CPU | 6 核 Kryo 260（2×A73 @ 2.0GHz + 4×A53 @ 1.8GHz 有效） |
| GPU | Adreno 610 |
| 内存 | 5782272 kB（~5.5 GB，内核限制 ~4044 MB 可用） |
| 存储 | eMMC `3H6CAB`，122142720 × 512B ≈ **58.3 GB** |
| 基带 | MPSS.AT.4.3.1（Qualcomm Modem，暂不适配） |
| Bootloader | 已解锁 (`verifiedbootstate=orange`) |
| HW Version | 1.9.0 (CN) |

### 2.2 显示

| 项目 | 值 | 来源 |
|------|-----|------|
| 分辨率 | 1080 × 2340 | cmdline / wm size |
| 密度 | 420 dpi | ro.sf.lcd_density |
| 面板 IC | Novatek **NT36672A** | dmesg |
| 面板厂商 | **Tianma（天马）** | dmesg: `[nt36672a video mode dsi tianma panel]` |
| 下游 panel 名 | `dsi_nt36672a_tianma_vid_display` | cmdline `msm_drm.dsi_display0=` |
| DSI 模式 | Video mode | dmesg |
| MDSS 基址 | `0x05e00000` | dmesg / mainline sm6125.dtsi |
| DSI Ctrl | `0x05e94000` | dmesg |
| DSI PHY | `0x05e94400` | dmesg |

### 2.3 触控（重要：SPI 非 I2C）

| 项目 | 值 | 来源 |
|------|-----|------|
| 触控 IC | Novatek **NT36672A** | dmesg: `TP info: [Vendor]tianma [IC]nt36672a` |
| 总线 | **SPI**（非 I2C！） | dmesg: `spi_geni 4a88000.spi` |
| SPI 控制器 | `spi@4a88000`（mainline: `&spi2`） | device-tree |
| Compatible | `focaltech,fts` + `novatek,NVT-ts-spi` | device-tree |
| SPI 频率 | **8 MHz** (`0x007a1200`) | device-tree |
| 中断 GPIO | **TLMM 88**，ACTIVE_LOW | device-tree / dmesg |
| 复位 GPIO | **TLMM 87** | device-tree |
| SW Reset 地址 | `0x03F0FE` | device-tree `novatek,swrst-n8-addr` |
| SPI Fast Read 地址 | `0x03F310` | device-tree `novatek,spi-rd-fast-addr` |
| 固件文件 | `novatek_ts_tianma_fw.bin`（118784 bytes） | dmesg / `/vendor/firmware/` |
| 输入设备名 | `NVTCapacitiveTouchScreen` | `/sys/class/input/` |
| IRQ 号 | 232 | dmesg |

**关键结论：** 主线 `novatek-nvt-ts` 驱动是 **I2C** 接口，**不能**直接用于 ginkgo。需要 `novatek,NVT-ts-spi` 的 SPI 驱动（目前仅存在于下游/out-of-tree）。

### 2.4 WiFi

| 项目 | 值 | 来源 |
|------|-----|------|
| 芯片 | Qualcomm **WCN3990** | 设备规格 / ICNSS 驱动 |
| 驱动（Android） | `icnss` + `cnss` | dmesg |
| 驱动（主线目标） | `ath10k_snoc` + `wcn36xx` | 主线内核 |
| 基址 | `0x0c800000` | device-tree `qcom,icnss@C800000` |
| MSA 内存 | `wlan_msa_region@53300000`，2 MB | dmesg |
| 固件 DSP | `wlanmdsp.mbn`（3720220 bytes） | `/vendor/firmware_mnt/image/` |
| Board 数据 | `bdwlan.bin`（26328 bytes） | `/vendor/firmware_mnt/image/` |
| FW Ready 标志 | `0xd87` | dmesg |

### 2.5 其他硬件（后续阶段）

| 组件 | 型号 | I2C 地址 | 总线 |
|------|------|----------|------|
| 音频 Codec | max98927 | 0x3a | i2c-0 |
| 音频功放 | tas2562 | 0x4c | i2c-0 |
| 指纹 | Goodix (`gdx`) | — | SPI/UART |
| LED 驱动 | ktd3136 | 0x36 | i2c-0 |
| USB-C 切换 | fsa4480 | 0x43 | i2c-0 |
| PMIC | PM6125 + PMI632 | SPMI | — |

### 2.6 SKU 差异

| 组件 | 本机 | 其他批次可能 |
|------|------|-------------|
| 面板 | Tianma NT36672A | Huaxing FT8719 等 |
| 主摄 | 待确认 | Samsung GM1/GM2、Sony IMX582 |
| 触控固件 | `novatek_ts_tianma_fw.bin` | `novatek_ts_ebbg_fw.bin` |
| 指纹 | Goodix | FPC1020 |

---

## 3. 存储与分区布局

### 3.1 关键分区

| 分区 | 用途 | 备注 |
|------|------|------|
| `xbl` / `xbl_config` | 主 Bootloader | 勿刷写 |
| `abl` | Android Bootloader | fastboot 入口 |
| `boot` | 内核 + ramdisk | **pmOS/Ubuntu 刷写目标** |
| `dtbo` | Device Tree Overlay | 主线一般不用 |
| `vbmeta` | Verified Boot 元数据 | 需 `--disable-verification` |
| `system` | 系统分区（当前 LineageOS） | 可复用或 repartition |
| `vendor` | 厂商分区 | 含固件 |
| `userdata` | 用户数据 | Ubuntu rootfs 可放此处 |
| `persist` | 持久化校准数据 | 保留 |
| `modem` | Modem 固件 | 暂不修改 |

### 3.2 刷机参数（参考 willow pmOS / 通用 Qualcomm）

```bash
# boot.img 格式
deviceinfo_flash_pagesize="4096"
deviceinfo_flash_offset_base="0x00000000"
deviceinfo_flash_offset_kernel="0x00008000"
deviceinfo_flash_offset_ramdisk="0x01000000"
deviceinfo_flash_offset_second="0x00f00000"
deviceinfo_flash_offset_tags="0x00000100"
deviceinfo_generate_bootimg="true"
deviceinfo_bootimg_qcdt="false"
deviceinfo_append_dtb="true"
```

### 3.3 Bootloader 状态

```
verifiedbootstate=orange    # 已解锁
slot_suffix=（空）           # 非 A/B 分区方案
secureboot=1                # 安全启动开启但 vbmeta 可禁用
```

---

## 4. 总线与地址映射

### 4.1 Mainline SM6125 总线对照

| 下游地址 | Mainline 节点 | 功能 | ginkgo 用途 |
|----------|--------------|------|-------------|
| `0x04a88000` | `&spi2` / `&i2c2` | QUP SE2（复用） | **触控 SPI** |
| `0x04a8c000` | `&i2c3` | QUP SE3 | 待查 |
| `0x04a90000` | `&uart4` | Debug UART | Serial console |
| `0x04e00000` | `&usb3` | USB DWC3 | adb / 网络 |
| `0x05e00000` | `&mdss` | 显示子系统 | DRM/DSI |
| `0x05e94000` | `&mdss_dsi0` | DSI 控制器 | 面板 |
| `0x05e94400` | `&mdss_dsi0_phy` | DSI PHY 14nm | 面板 |
| `0x0c800000` | **缺失**（待添加） | WCN3990 WiFi | WiFi |
| `0x04744000` | `&sdhc_1` | eMMC | 存储 |

### 4.2 I2C 设备映射（实测 i2c-0）

| 地址 | 设备 | 驱动 |
|------|------|------|
| 0x08 | i2c-pmic | PMIC I2C |
| 0x09 | i2c-pmic | PMIC I2C |
| 0x36 | ktd3136 | LED |
| 0x3a | max98927L | 音频 Codec |
| 0x43 | fsa4480-i2c | USB-C 切换 |
| 0x4c | tas2562 | 音频功放 |

### 4.3 内核 cmdline 参考

```
console=ttyMSM0,115200n8
earlycon=msm_serial_dm,0x4a90000
androidboot.hardware=qcom
androidboot.bootdevice=4744000.sdhci
androidboot.usbcontroller=4e00000.dwc3
msm_drm.dsi_display0=dsi_nt36672a_tianma_vid_display:
```

主线建议 cmdline（初期）：

```
console=ttyMSM0,115200n8
earlycon
root=/dev/mmcblk0pXX
rw
```

---

## 5. 主线内核现状与差距

### 5.1 功能矩阵

| 功能 | 主线 ginkgo DTS | 主线驱动 | 下游 Android | 差距 |
|------|----------------|---------|-------------|------|
| Boot / Serial | ✅ | ✅ | ✅ | 无 |
| eMMC | ✅ | ✅ | ✅ | 无 |
| USB | ✅ | ✅ | ✅ | 无 |
| SD 卡 | ✅ | ✅ | ✅ | 无 |
| 按键 | ✅ | ✅ | ✅ | 无 |
| Simple FB | ✅ | ✅ | — | 临时方案 |
| **DRM/DSI 显示** | ❌ | ✅ 驱动存在 | ✅ | **需 DTS + 面板 timing** |
| **触控 SPI** | ❌ | ❌ 无 SPI 驱动 | ✅ ts_nt36xxx | **需新驱动或 port** |
| **WiFi** | ❌ | ✅ 驱动存在 | ✅ icnss | **需 SoC wifi 节点 + DTS + 固件** |
| 音频 | ❌ | 部分 | ✅ | 后续 |
| Modem | ❌ | 部分 | ✅ | 后续 |

### 5.2 主线已有 ginkgo patch 历史

| 日期 | 作者 | 内容 |
|------|------|------|
| 2025-03 | Gabriel Gonzales | 初始 ginkgo DTS |
| 2026-01 | Barnabás Czémán | reserved memory 修正、ginkgo-common.dtsi、framebuffer memory-region |
| 2026-01 | Biswapriyo Nath | Volume Up 修正、RTC、Debug UART |
| 2026-03 | Biswapriyo Nath | 振动、红外、USB-C OTG |

### 5.3 SM6125 vs SM6115 平台差异（WiFi 关键）

`sm6115.dtsi` **有** `wifi@c800000` 节点，`sm6125.dtsi` **没有**。

sm6115 wifi 节点模板（需移植到 sm6125.dtsi）：

```dts
wifi: wifi@c800000 {
    compatible = "qcom,wcn3990-wifi";
    reg = <0x0 0x0c800000 0x0 0x800000>;
    reg-names = "membase";
    memory-region = <&wlan_msa_mem>;
    interrupts = <GIC_SPI 358 IRQ_TYPE_LEVEL_HIGH>,
                 /* ... 共 12 个 CE 中断 ... */;
    iommus = <&apps_smmu 0x1a0 0x1>;
    qcom,msa-fixed-perm;
    status = "disabled";
};
```

---

## 6. 显示子系统（DRM/DSI）

### 6.1 硬件连接

```
MDSS (0x5e00000)
  └── MDP/DPU (0x5e01000)
        └── DSI0 (0x5e94000) ── DSI lanes x4 ──► NT36672A (Tianma)
              └── PHY (0x5e94400, 14nm)
```

### 6.2 下游电源（来自 device-tree）

| Supply | 说明 |
|--------|------|
| `ibb-supply` | 面板 IBB 负电压（LCDB NCP） |
| `lab-supply` | 面板 LAB 正电压（LCDB LDO） |
| `vddio-supply` | 面板 IO 电压 → `vreg_l9a` (1.8V) |

### 6.3 主线面板驱动

- 驱动：`drivers/gpu/drm/panel/panel-novatek-nt36672a.c`
- 现有 compatible：`tianma,fhd-video` + `novatek,nt36672a`
- 现有分辨率：**1080×2246**（Poco F1）
- **ginkgo 需要：1080×2340** — 新 timing + 可能新 init 序列

### 6.4 待完成工作

1. **从下游提取** ginkgo Tianma NT36672A 的 DSI init 序列（下游 `dsi_nt36672a_tianma_vid_display` 面板驱动源码）
2. **扩展** `panel-novatek-nt36672a.c` 或新建 `tianma,fhd-video-ginkgo` compatible
3. **在 ginkgo DTS 添加**（参考 laurel）：
   - `&mdss { status = "okay"; }`
   - `&mdss_dsi0 { ... panel@0 { ... } }`
   - `&mdss_dsi0_phy { status = "okay"; }`
   - 面板 regulator（GPIO 控制的 LDO）
   - pinctrl for reset GPIO
4. **验证** `modetest -M msm_drm -s <connector>:1080x2340`

### 6.5 参考：laurel 主线 MDSS DTS 结构

```dts
&mdss { status = "okay"; };

&mdss_dsi0 {
    vdda-supply = <&vreg_l18a>;
    status = "okay";
    panel@0 {
        compatible = "samsung,s6e8fc0-m1906f9";
        reg = <0>;
        reset-gpios = <&tlmm 90 GPIO_ACTIVE_LOW>;
        vdd-supply = <&panel_vdd_1p8>;
        vci-supply = <&panel_vci_3p0>;
        port {
            panel_in: endpoint {
                remote-endpoint = <&mdss_dsi0_out>;
            };
        };
    };
};

&mdss_dsi0_out {
    data-lanes = <0 1 2 3>;
    remote-endpoint = <&panel_in>;
};

&mdss_dsi0_phy { status = "okay"; };
```

### 6.6 面板 variant 注意

/vendor/etc/ 中存在多个 QDCM 校准文件：
- `qdcm_calib_data_nt36672a_video_mode_dsi_tianma_panel.xml`（本机）
- `qdcm_calib_data_nt36672a_video_mode_dsi_shenchao_panel.xml`（神彩 variant）

---

## 7. 触控子系统（SPI）

### 7.1 关键发现：SPI 非 I2C

ginkgo 触控通过 **SPI** 连接，compatible 为 `novatek,NVT-ts-spi`。

主线 `drivers/input/touchscreen/novatek-nvt-ts.c` 是 **I2C-only**，**不能直接使用**。

### 7.2 设备树参数（实测）

```dts
&spi2 {   /* 下游 spi@4a88000，mainline &spi2 */
    status = "okay";

    touchscreen@0 {
        compatible = "focaltech,fts", "novatek,NVT-ts-spi";
        reg = <0>;

        spi-max-frequency = <8000000>;
        spi-cs-high;

        novatek,irq-gpio = <&tlmm 88 GPIO_ACTIVE_LOW>;
        novatek,reset-gpio = <&tlmm 87 GPIO_ACTIVE_HIGH>;

        novatek,swrst-n8-addr = <0x03f0fe>;
        novatek,spi-rd-fast-addr = <0x03f310>;

        touch_vddio-supply = <&...>;  /* vreg_l18a 或类似 */
        touch_lab-supply = <&...>;    /* LCDB LDO */
        touch_ibb-supply = <&...>;    /* LCDB NCP */
    };
};
```

### 7.3 驱动选项

| 方案 | 描述 | 主线合规 | 难度 |
|------|------|---------|------|
| A. Port 下游 `nt36xxx_spi` | 从 Lineage/CAF 内核移植 `nt36xxx_spi` 驱动 | ❌ out-of-tree | 中 |
| B. 新写 SPI 驱动 upstream | 基于 `novatek-nvt-ts` 扩展 SPI 支持 | ✅ | 高 |
| C. 社区 OOT 模块 | 类似 willow pmOS 的 `ts_nt36xxx` 模块但改 SPI | ❌ | 中 |

**推荐路线：** 短期用方案 A 验证功能，长期推方案 B upstream。

### 7.4 固件

| 文件 | 大小 | 路径 |
|------|------|------|
| `novatek_ts_tianma_fw.bin` | 118784 B | `/vendor/firmware/` |
| `novatek_ts_tianma_mp.bin` | 118784 B | `/vendor/firmware/`（产测用） |
| `novatek_ts_ebbg_fw.bin` | 118784 B | 其他批次面板 |

下游驱动在 boot 时加载固件（dmesg: `Update firmware success! <105436 us>`）。

### 7.5 Trinket 平台参考

realme5 `trinket-idp.dtsi`（同 SM6125/trinket 平台）使用相同 GPIO：
- IRQ: TLMM 88
- Reset: TLMM 87
- Compatible: `novatek,NVT-ts-spi`
- SW Reset: `0x03F0FE`

这验证了 ginkgo 触控配置与 trinket 参考板一致。

---

## 8. WiFi 子系统（WCN3990/ICNSS）

### 8.1 硬件

| 项目 | 值 |
|------|-----|
| 芯片 | WCN3990 |
| 接口 | SNOC（Shared Network-on-Chip） |
| 基址 | `0x0c800000`，大小 8 MB |
| MSA 内存 | `0x53300000`，2 MB（`wlan_msa_mem`） |
| 中断 | CE0–CE11（GIC SPI 358–369，待核实 sm6125） |

### 8.2 下游 ICNSS 电源（device-tree）

| Supply 属性 | 说明 |
|-------------|------|
| `vdd-cx-mx-supply` | CX/MX 0.8V |
| `vdd-1.8-xo-supply` | XO 1.8V |
| `vdd-1.3-rfa-supply` | RFA 1.3V |
| `vdd-3.3-ch0-supply` | CH0 3.3V |

对应 sm6115 参考（PM6125 regulators）：

```dts
&wifi {
    vdd-0.8-cx-mx-supply = <&vreg_l8a>;   /* 或 pm6125_l8a */
    vdd-1.8-xo-supply = <&vreg_l16a>;
    vdd-1.3-rfa-supply = <&vreg_l17a>;
    vdd-3.3-ch0-supply = <&vreg_l23a>;
    qcom,calibration-variant = "XXX_Ginkgo";  /* 待确定 */
    status = "okay";
};
```

### 8.3 固件

已从设备提取（存放于 `firmware/ginkgo/` 参考目录）：

| 文件 | 大小 | 主线安装路径 |
|------|------|-------------|
| `wlanmdsp.mbn` | 3,720,220 B | `/lib/firmware/qcom/` 或 ath10k 子目录 |
| `bdwlan.bin` | 26,328 B | 转换为 `board-2.bin` → `/lib/firmware/ath10k/WCN3990/hw1.0/` |
| `firmware-5.bin` | 待提取 | `/lib/firmware/ath10k/WCN3990/hw1.0/` |

**bdwlan → board-2.bin 转换：**

```bash
# 参考 Konrad Dybcio 的方法
# https://github.com/jhugo/linux/blob/5.5rc2_wifi/README
python3 bdwlan_to_board2.py bdwlan.bin > board-2.bin
```

**firmware-5.bin 来源：** 需从 ath10k-firmware 仓库或设备进一步匹配。注意 wlanmdsp.mbn 与 firmware-5.bin 版本必须一致，否则 ath10k 报 `SINGLE_CHAN_INFO` 等错误。

### 8.4 主线驱动链

```
Device Tree (&wifi)
  → ath10k_snoc (platform driver)
    → wcn36xx (WCN3990 specific)
      → ath10k_core
        → mac80211/cfg80211
          → wlan0
```

内核配置需求：

```
CONFIG_ATH10K=y/m
CONFIG_ATH10K_SNOC=y/m
CONFIG_WCN36XX=y/m
CONFIG_QCOM_WCNSS_PIL=y/m
```

### 8.5 已知主线问题

- WCN3990 A-MSDU 分片 bug：高负载下吞吐量崩溃（2026 年 7–8 月 RFC patch 中）
- firmware-5.bin 与 wlanmdsp.mbn 版本不匹配问题（ath10k 邮件列表 2023-12）
- SM6125 尚无 wifi 节点 — **需先 upstream sm6125.dtsi wifi 节点**

### 8.6 待完成工作

1. 向 `sm6125.dtsi` 添加 `wifi@c800000` 节点（参考 sm6115，调整 interrupt 号）
2. 在 `sm6125-xiaomi-ginkgo-common.dtsi` 添加 `&wifi { status = "okay"; ... }`
3. 确定 `qcom,calibration-variant` 字符串（从 bdwlan 或下游 cnss 配置提取）
4. 提取并安装全套固件
5. 创建 `linux-firmware` 或 device-specific firmware 包
6. 调试 ath10k probe 和连接

---

## 9. Reserved Memory 布局

### 9.1 主线 ginkgo-common.dtsi（当前）

| 区域 | 地址 | 大小 | 用途 |
|------|------|------|------|
| adsp_pil_mem | 0x55300000 | 34 MB | ADSP PIL |
| ipa_fw_mem | 0x57500000 | 64 KB | IPA 固件 |
| ipa_gsi_mem | 0x57510000 | 20 KB | IPA GSI |
| gpu_mem | 0x57515000 | 8 KB | GPU |
| framebuffer_mem | 0x5c000000 | ~9.9 MB | Simple FB / 显示 |
| ramoops | 0x61600000 | 4 MB | 内核日志 |

### 9.2 下游额外区域（参考）

| 区域 | 地址 | 大小 | 用途 |
|------|------|------|------|
| wlan_msa_mem | 0x53300000 | 2 MB | WiFi MSA |
| modem_region | 0x4b000000 | 126 MB | Modem |
| camera_region | 0x4ab00000 | 5 MB | 相机 |
| cdsp_regions | 0x53500000 | 30 MB | CDSP |

**注意：** reserved memory 定义错误会导致高负载崩溃。Barnabás Czémán 2026 年 patch 修正了 ginkgo 的布局。验证时使用 `memtest=1` 内核参数。

---

## 10. 固件清单与提取

### 10.1 已从设备提取

| 文件 | 大小 | 用途 | 本地路径 |
|------|------|------|----------|
| `bdwlan.bin` | 26,328 B | WiFi board 数据 | `firmware/ginkgo/bdwlan.bin` |
| `wlanmdsp.mbn` | 3,720,220 B | WiFi DSP 固件 | `firmware/ginkgo/wlanmdsp.mbn` |
| `novatek_ts_tianma_fw.bin` | 118,784 B | 触控固件 | `firmware/ginkgo/novatek_ts_tianma_fw.bin` |

### 10.2 待提取

| 文件 | 路径 | 用途 |
|------|------|------|
| `firmware-5.bin` | 待从 bdwlan 或 ath10k-firmware 匹配 | WiFi MAC 固件 |
| `bdwlan.bXX` | `/vendor/firmware_mnt/image/bdwlan.*` | 确定 board ID |
| ADSP/Modem 固件 | `/vendor/firmware_mnt/image/` | 后续音频/Modem |

### 10.3 提取命令

```bash
# 需要 root
adb shell su -c 'cp /vendor/firmware/novatek_ts_tianma_fw.bin /data/local/tmp/'
adb shell su -c 'cp /vendor/firmware_mnt/image/wlanmdsp.mbn /data/local/tmp/'
adb shell su -c 'cp /vendor/firmware_mnt/image/bdwlan.bin /data/local/tmp/'
adb pull /data/local/tmp/novatek_ts_tianma_fw.bin firmware/ginkgo/
adb pull /data/local/tmp/wlanmdsp.mbn firmware/ginkgo/
adb pull /data/local/tmp/bdwlan.bin firmware/ginkgo/
```

---

## 11. 主线 DTS 现状与待补全项

### 11.1 当前 `sm6125-xiaomi-ginkgo-common.dtsi` 已有

- [x] `qcom,msm-id`
- [x] Simple framebuffer (1080×2340)
- [x] Reserved memory（修正后）
- [x] GPIO keys（Volume Up / Power / Volume Down）
- [x] Regulators（RPM PM6125 全套）
- [x] eMMC (sdhc_1) + SD 卡 (sdhc_2)
- [x] USB (usb3, hsusb_phy1)
- [x] Debug UART (uart4)
- [x] ramoops

### 11.2 待添加到 ginkgo DTS

- [ ] `&mdss` + `&mdss_dsi0` + panel 节点
- [ ] 面板 regulator（GPIO LDO）
- [ ] 面板 pinctrl（reset GPIO）
- [ ] `&spi2` + touchscreen SPI 节点
- [ ] 触控 regulator
- [ ] `&wifi` 节点（依赖 sm6125.dtsi 先有 wifi 节点）
- [ ] WiFi regulator 配置
- [ ] `qcom,calibration-variant`

### 11.3 待添加到 `sm6125.dtsi`（SoC 级，影响所有 SM6125 设备）

- [ ] `wifi@c800000` 节点（从 sm6115 移植）

---

## 12. 参考移植案例

### 12.1 同平台 SM6125 主线设备

| 设备 | 显示 | 触控 | WiFi | 仓库 |
|------|------|------|------|------|
| laurel (Mi A3) | ✅ Samsung S6E8FC0 | ✅ FocalTech ft3518 (I2C) | ❌ | mainline DTS |
| seine (Xperia 10 II) | ✅ Samsung SOFEF01 | 部分 | ❌ | mainline DTS |
| **ginkgo (本机)** | ❌ | ❌ | ❌ | mainline DTS |

### 12.2 同平台下游参考

| 来源 | 用途 |
|------|------|
| `realme5-kernel trinket-idp.dtsi` | 触控 SPI GPIO 88/87、NT36672 面板列表 |
| `rjeli/lineage_kernel_sm6125` | ginkgo/willow 完整下游 DTS |
| `willow pmOS (device/testing/)` | 下游 boot 流程、触控模块、固件包 |
| `Xiaomi-trinket-dev/vendor_xiaomi_sm6125-common` | 触控/面板固件 |

### 12.3 WiFi 参考（SM6115，同 PM6125）

| 设备 | calibration-variant | 仓库 |
|------|-------------------|------|
| Lenovo P11 (j606f) | `Lenovo_P11` | sm6115p-lenovo-j606f.dts |
| F(x)tec Pro1X | `Fxtec_QX1050` | sm6115-fxtec-pro1x.dts |

---

## 13. Ubuntu 26.04 集成方案

### 13.1 版本信息

| 项目 | 值 |
|------|-----|
| Ubuntu 版本 | 26.04 LTS (Resolute Raccoon) |
| 默认内核 | Linux 7.0 |
| 架构 | arm64（已进主 archive，非 ports） |
| 官方手机支持 | **无** |

### 13.2 构建流程概览

```
1. 获取 Linux 7.0 源码
2. 确认 ginkgo DTS 在主线中（或打 patch）
3. 配置内核（DRM_MSM, ATH10K_SNOC, 等）
4. 交叉编译 Image.gz + sm6125-xiaomi-ginkgo.dtb
5. 构建 initramfs（含固件、模块）
6. 制作 boot.img（mkbootimg）
7. debootstrap Ubuntu 26.04 arm64 rootfs
8. fastboot flash boot boot.img
9. 刷写 rootfs 到 userdata 分区
10. fastboot flash vbmeta --disable-verification vbmeta.img
```

### 13.3 内核配置要点

```
CONFIG_ARCH_QCOM=y
CONFIG_SERIAL_MSM=y
CONFIG_SERIAL_MSM_CONSOLE=y
CONFIG_DRM_MSM=y
CONFIG_DRM_MSM_DSI=y
CONFIG_DRM_PANEL_NOVATEK_NT36672A=y
CONFIG_ATH10K=m
CONFIG_ATH10K_SNOC=m
CONFIG_WCN36XX=m
CONFIG_MMC_SDHCI_MSM=y
CONFIG_USB_DWC3=y
CONFIG_USB_DWC3_QCOM=y
# 触控 SPI 驱动（待添加或 OOT）
```

### 13.4 Rootfs 选项

| 方案 | 描述 |
|------|------|
| A. Ubuntu minimal | debootstrap + systemd，无桌面 |
| B. Ubuntu Desktop | GNOME/Wayland，需 P2/P3 完成 |
| C. postmarketOS rootfs | 复用 pmOS 构建系统，换 Ubuntu 源 |

---

## 14. Upstream Patch 清单

按优先级排列，需提交到 linux-arm-msm / ~postmarketos/upstreaming：

| # | Patch 标题 | 目标文件 | 优先级 | 状态 |
|---|-----------|---------|--------|------|
| 1 | arm64: dts: qcom: sm6125: Add WCN3990 WiFi node | `sm6125.dtsi` | P0 | 待做 |
| 2 | arm64: dts: qcom: sm6125-xiaomi-ginkgo: Enable MDSS and Tianma panel | ginkgo DTS | P1 | 待做 |
| 3 | drm: panel: novatek-nt36672a: Add ginkgo 1080x2340 Tianma panel | panel driver | P1 | 待做 |
| 4 | arm64: dts: qcom: sm6125-xiaomi-ginkgo: Add NT36672A SPI touchscreen | ginkgo DTS | P2 | 待做 |
| 5 | input: touchscreen: Add Novatek NVT SPI driver | 新驱动或扩展现有 | P2 | 待做 |
| 6 | arm64: dts: qcom: sm6125-xiaomi-ginkgo: Enable WCN3990 WiFi | ginkgo DTS | P3 | 待做 |
| 7 | arm64: dts: qcom: sm6125-xiaomi-ginkgo: Add WiFi calibration variant | ginkgo DTS | P3 | 待做 |

---

## 15. 分阶段实施计划

### 阶段 1：Boot + Ubuntu minimal（2–4 周）

- [ ] 搭建交叉编译环境
- [ ] 编译 Linux 7.0 + ginkgo DTB
- [ ] 制作 boot.img + initramfs
- [ ] debootstrap Ubuntu 26.04 arm64
- [ ] fastboot 刷入，serial console 登录
- [ ] 验证 eMMC / USB / 按键

**验收：** serial 上 `uname -a` 显示 Linux 7.0，`systemd` 运行

### 阶段 2：DRM 显示（4–8 周）

- [ ] 从下游提取 Tianma NT36672A init 序列
- [ ] 扩展 panel-novatek-nt36672a 支持 1080×2340
- [ ] 编写 ginkgo MDSS/DSI/panel DTS
- [ ] 添加面板 regulator 和 pinctrl
- [ ] 验证 `modetest` 和 simple 图形输出
- [ ] 提交 upstream patch

**验收：** 屏幕显示内容，不再仅 simplefb

### 阶段 3：触控 SPI（3–6 周，与阶段 2 部分并行）

- [ ] Port 或编写 NT36672A SPI 触控驱动
- [ ] 编写 ginkgo SPI touchscreen DTS
- [ ] 集成触控固件加载
- [ ] 验证 `/dev/input/event*` 触摸事件
- [ ] 在 Weston/GNOME 中测试触控

**验收：** 手指触摸有响应，UI 可操作

### 阶段 4：WiFi（2–4 月）

- [ ] 向 sm6125.dtsi 添加 wifi 节点（upstream）
- [ ] 编写 ginkgo &wifi DTS
- [ ] 提取/转换全套 WiFi 固件
- [ ] 确定 calibration-variant
- [ ] 调试 ath10k probe
- [ ] 验证扫描和连接 AP
- [ ] 处理 WCN3990 A-MSDU bug（如需要）

**验收：** `iw dev wlan0 scan` 有结果，可连接 WiFi

### 阶段 5：Ubuntu Desktop 联调（2–4 周）

- [ ] 安装 GNOME/Wayland 或 chosen DE
- [ ] 配置 NetworkManager + WiFi
- [ ] 配置 libinput + 触控
- [ ] 整体稳定性测试

**验收：** Ubuntu 桌面完整可用，触控 + WiFi 正常

---

## 16. 风险登记册

| ID | 风险 | 影响 | 概率 | 缓解措施 |
|----|------|------|------|----------|
| R1 | SM6125 wifi 节点 upstream 被拒或延迟 | WiFi 无法主线化 | 中 | 先用 OOT patch 验证 |
| R2 | 触控 SPI 驱动需全新开发 | 触控延期 2–3 月 | 高 | 短期 port 下游驱动验证 |
| R3 | 面板 1080×2340 init 序列难以提取 | 显示不亮 | 中 | 使用 mdss-dsi-panel-generator |
| R4 | WiFi 固件版本不匹配 | ath10k 不工作 | 高 | 严格匹配 wlanmdsp + firmware-5 |
| R5 | 面板/触控 SKU 差异 | 部分设备不兼容 | 中 | 多 variant DT overlay |
| R6 | WCN3990 A-MSDU bug | WiFi 高负载崩溃 | 中 | 跟踪 2026 RFC patch |
| R7 | reserved memory 错误 | 随机崩溃 | 低 | memtest=1 验证，参考已合并 patch |

---

## 17. 附录：原始采集数据

### A. dmesg 面板关键行

```
mdss_pll_probe: MDSS pll label = MDSS DSI 0 PLL
msm-dsi-panel: [nt36672a video mode dsi tianma panel] fallback to default te-pin-select
msm-dsi-display: Successfully bind display panel 'dsi_nt36672a_tianma_vid_display'
[drm] Initialized msm_drm 1.2.0 for 5e00000.qcom,mdss_mdp on minor 0
```

### B. dmesg 触控关键行

```
[NVT-ts] TP info: [Vendor]tianma [IC]nt36672a
[NVT-ts] nvt_parse_dt: novatek,irq-gpio=88
spi_geni 4a88000.spi: proto 1
[NVT-ts] nvt_ts_probe: request irq 232 succeed
[NVT-ts] update_firmware_request: filename is novatek_ts_tianma_fw.bin
[NVT-ts] nvt_update_firmware: Update firmware success!
input: NVTCapacitiveTouchScreen
```

### C. dmesg WiFi 关键行

```
icnss: Platform driver probed successfully
icnss: WLAN FW is ready: 0xd87
OF: reserved mem: initialized node wlan_msa_region@53300000
```

### D. 输入设备列表

```
qpnp_pon                          # 电源键
NVTCapacitiveTouchScreen          # 触控
uinput-goodix                     # 指纹
gpio-keys                         # 音量键
```

### E. 关键仓库链接

| 资源 | URL |
|------|-----|
| Mainline ginkgo DTS | https://github.com/torvalds/linux/tree/master/arch/arm64/boot/dts/qcom/sm6125-xiaomi-ginkgo.dts |
| panel-novatek-nt36672a | https://github.com/torvalds/linux/tree/master/drivers/gpu/drm/panel/panel-novatek-nt36672a.c |
| sm6125-mainline fork | https://gitlab.com/sm6125-mainline/linux |
| willow pmOS device | https://gitlab.com/postmarketOS/pmaports/-/tree/master/device/testing/device-xiaomi-willow |
| trinket-idp.dtsi 参考 | https://github.com/realme-kernel-opensource/realme5-kernel-source |
| ath10k-firmware WCN3990 | https://github.com/kvalo/ath10k-firmware/tree/master/WCN3990 |
| bdwlan 转换工具 | https://github.com/jhugo/linux/blob/5.5rc2_wifi/README |
| mdss-dsi-panel-generator | https://github.com/msm8916-mainline/linux-mdss-dsi-panel-driver-generator |
| postmarketOS ginkgo wiki | https://wiki.postmarketos.org/wiki/Xiaomi_Redmi_Note_8_(xiaomi-ginkgo) |
| Ubuntu 26.04 arm64 | https://ubuntu.com/download/server/arm |

---

*本文档随 port 进展持续更新。关联文档：[postmarketOS-ginkgo-research.md](./postmarketOS-ginkgo-research.md)*
