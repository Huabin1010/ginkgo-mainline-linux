**语言：** [English](../postmarketOS-ginkgo-research.md) | 简体中文

# postmarketOS 适配调研：Redmi Note 8 (ginkgo)

> 调研日期：2026-08-04（初版）、2026-08-04（主线 + Ubuntu 专项）  
> 目标设备：Xiaomi Redmi Note 8 (codename: `ginkgo`)  
> 项目：`Kernel-Build`  
> **当前目标：** 主线 Linux → 启动 Ubuntu 26.04 LTS → 可用触控 + WiFi

---

## 1. 结论摘要

| 维度 | 状态 |
|------|------|
| **主线内核 (Mainline)** | 已有基础支持，可引导，但功能有限 |
| **postmarketOS 官方包** | **尚无** `device-xiaomi-ginkgo`，未进入 community/main |
| **最接近参考** | 姊妹机 **willow (Redmi Note 8T)** 在 pmaports `testing` 有完整下游方案 |
| **完整适配难度** | **中高** — SoC 同平台已有积累，但显示/触控/WiFi/音频/Modem 仍需大量工作 |
| **当前优势** | Bootloader 已解锁、有 root、已在跑自定义 `4.14.117-perf+` 内核 |

**一句话总结：** 可以开始做 port，但「完整适配」应分阶段推进——先复用 willow/下游内核打通 boot，再逐步切到 mainline 并补齐各子系统。

> **主线专项文档：** 详见 [mainline-ginkgo-porting-guide.md](./mainline-ginkgo-porting-guide.md)（硬件清单、DTS 差距、触控 SPI、WiFi 固件、分阶段计划）

---

## 2. 目标设备硬件画像

以下信息通过 adb 实测与 dmesg 确认（设备序列号 `<serial>`）。

### 2.1 基本信息

| 项目 | 值 |
|------|-----|
| 型号 | Redmi Note 8 |
| Codename | `ginkgo` |
| SoC | Qualcomm SM6125 / trinket（骁龙 665） |
| CPU | 6 核 Kryo 260 (4×A73 + 2×A53 有效核) |
| GPU | Adreno 610 |
| 内存 | ~5.5 GB（内核限制约 4044 MB） |
| 存储 | eMMC（`4744000.sdhci`），非 UFS |
| 屏幕 | 1080 × 2340，420 dpi |
| Bootloader | 已解锁 (`verifiedbootstate=orange`) |
| 当前 ROM | LineageOS 17.1 (`17.1-20220214-NIGHTLY-ginkgo`) |
| 当前内核 | `4.14.117-perf+`（2026-06-01 自编译） |

### 2.2 关键硬件组件

| 组件 | 实测配置 | 备注 |
|------|----------|------|
| 显示面板 | **NT36672A + Tianma** | cmdline: `msm_drm.dsi_display0=dsi_nt36672a_tianma_vid_display` |
| 触控 | **Novatek NT36672A** | 与 willow 同系列，下游驱动 `ts_nt36xxx` |
| 指纹 | **Goodix** | `androidboot.fpsensor=gdx` |
| WiFi | **WCN3990 / ICNSS** | `qcom,icnss`，需固件 + remoteproc |
| 音频 Codec | **max98927** | I2C 音频 codec |
| 音频功放 | **tas2562** | I2C 功放 |
| PMIC | **PM6125 + PMI632** | 电源管理 |
| 基带 | Qualcomm MPSS | Modem 固件依赖，适配难度高 |

### 2.3 SKU 差异（需注意）

Redmi Note 8 存在多个硬件批次，不同 unit 组件可能不同：

| 组件 | 可能的 variant |
|------|----------------|
| 主摄像头 | Samsung GM1 / GM2，或 Sony IMX582 |
| 副摄像头 | GC02M1、OV13855、S5K4H7 等 |
| 显示面板 | Tianma NT36672A，或 Huaxing FT8719 等 |
| 指纹 | Goodix，或 FPC1020（其他批次） |

完整适配时需考虑多 variant 的 DT overlay 或 runtime 检测。

---

## 3. postmarketOS 生态现状

### 3.1 pmaports 中相关设备包

调研基于 pmaports upstream master（2026-08-04 clone）。

| 包名 | 位置 | 状态 | 说明 |
|------|------|------|------|
| `device-xiaomi-ginkgo` | — | **不存在** | 尚未 upstream，需新建 |
| `device-xiaomi-willow` | `device/testing/` | 已有 | Redmi Note 8T，**最佳参考** |
| `device-xiaomi-laurel` | `device/testing/` | 已有 | Mi A3，同 SM6125，用 mainline 内核 |
| `linux-postmarketos-qcom-sm6125` | `device/testing/` | 已有 | SM6125 mainline 6.1 fork |
| `linux-xiaomi-willow` | `device/testing/` | 已有 | 下游内核 4.14.117 |
| `firmware-xiaomi-willow` | `device/testing/` | 已有 | Novatek 触控固件 |
| `device-motorola-def` | `device/testing/` | 已有 | 同 trinket 平台，下游方案 |

### 3.2 官方 Wiki

设备页已存在（浏览器访问）：

- 设备页：https://wiki.postmarketos.org/wiki/Xiaomi_Redmi_Note_8_(xiaomi-ginkgo)
- SoC 页：https://wiki.postmarketos.org/wiki/Qualcomm_Snapdragon_665_(SM6125)

Wiki 使用 Anubis 反爬保护，需浏览器 + JavaScript 访问。

### 3.3 姊妹机 willow 方案详情

Redmi Note 8T (`willow`) 与 ginkgo 共用 `sm6125-xiaomi-ginkgo-common.dtsi`，主要差异是 willow 有 NFC。

willow 的 pmOS 策略（`device/testing/device-xiaomi-willow`）：

```
下游内核 4.14.117 (linux-xiaomi-willow)
  ├── 源码：rjeli/lineage_kernel_sm6125
  ├── 触控：ts_nt36xxx 内核模块 + novatek 固件
  ├── 显示：Weston DRM 后端 + weston-fixes.sh
  ├── 屏幕：1080×2340（与 ginkgo 相同）
  └── deviceinfo_no_framebuffer="true"
```

**对 ginkgo 的意义：** 最快路径是 fork willow 包，改 codename/DTB/defconfig，而非从零写 mainline port。

---

## 4. 主线内核 (Mainline Linux) 进展

ginkgo 主线支持由 postmarketOS 社区活跃推动，patch 的 CC 列表包含 `~postmarketos/upstreaming`。

### 4.1 关键 patch 时间线

| 时间 | 作者 | 内容 |
|------|------|------|
| 2025-03 | Gabriel Gonzales (semfault) | 初始 DTS：`sm6125-xiaomi-ginkgo.dts` |
| 2026-01 | Barnabás Czémán (barni2000) | 与 willow 共用 `ginkgo-common.dtsi`；修正 reserved memory、GPIO、framebuffer |
| 2026-01 | Biswapriyo Nath | 修正 Volume Up 键、启用 RTC、Debug UART |
| 2026-03~07 | Biswapriyo Nath | 振动器、红外发射器、USB-C OTG（已 Applied 到主线） |

### 4.2 主线当前已支持功能

| 功能 | 状态 | 说明 |
|------|------|------|
| 基础引导 | ✅ | 可启动到 shell |
| Simple framebuffer | ✅ | 继承 bootloader 预配置画面 |
| USB | ✅ | 基础 USB 功能 |
| eMMC | ✅ | 内部存储 |
| SD 卡 | ✅ | 外置 microSD |
| PMIC/RPM 调节器 | ✅ | 电源轨 |
| 电源/音量键 | ✅ | gpio-keys |
| Debug UART | ✅ | ttyMSM0 @ 115200 |
| RTC | ✅ | pm6125 RTC |
| 振动器 | ✅ | PMI632 vibrator（新 patch） |
| 红外发射 | ✅ | IR SPI LED（新 patch） |
| USB-C OTG | ✅ | Type-C 端口（新 patch） |

### 4.3 主线尚未支持 / 缺失功能

| 功能 | 状态 | 主要难点 |
|------|------|----------|
| DRM/DSI 显示 | ❌ | 仅 simplefb，无 MSM DRM 面板节点；Tianma 面板需单独适配 |
| 触控 | ❌ | NT36672A 无主线驱动；willow 用下游 `ts_nt36xxx` + 固件 |
| WiFi | ❌ | ICNSS + WCN3990 需固件、remoteproc、驱动链 |
| 蓝牙 | ❌ | 依赖 WCN3990 固件 |
| 音频 | ❌ | max98927/tas2562 + QDSP/ADSP，无 SM6125 专用 ASoC 驱动 |
| Modem | ❌ | IPA + MPSS 固件，复杂度极高 |
| 相机 | ❌ | 多 SKU 传感器 + CSIPHY/ISP |
| GPU 加速 | ❌ | Adreno 610 无可用 mainline 3D 驱动 |
| 指纹 | ❌ | Goodix 无主线支持 |
| 传感器 | ❌ | 加速度计/陀螺仪/接近传感器等需 IIO 驱动 |

**内核配置说明：** `linux-postmarketos-qcom-sm6125` 虽启用了 `CONFIG_DRM_MSM`，但设置了 `CONFIG_DRM_NOMODESET=y`，且缺少 SM6125 专用 sound 驱动。

---

## 5. 完整适配差距分析

### 5.1 分阶段路线图

```
Phase 0 (已有)     Bootloader 解锁 + 自定义内核 + 主线基础 DTS
       ↓
Phase 1 (1-2 周)   device-xiaomi-ginkgo 包 + boot.img + 首次刷机
       ↓
Phase 2 (2-4 周)   显示 (DRM/DSI) + 触控 (NT36672A) + 基础 UI
       ↓
Phase 3 (1-3 月)   WiFi + 音频 + 蓝牙 + 电池管理
       ↓
Phase 4 (3-6 月+)  Modem 通话/移动数据
       ↓
Phase 5 (6 月+)    相机 + 传感器 + 指纹
```

### 5.2 各阶段工作量估算

| 阶段 | 目标 | 预估工作量 | 主要依赖 |
|------|------|------------|----------|
| P0 | Fastboot 刷入 pmOS，serial/shell 登录 | 1–2 周 | pmaports 包、boot 配置 |
| P1 | 显示 + 触控 + 基础桌面 (Weston/Phosh) | 2–4 周 | 下游内核或 DRM 主线化 |
| P2 | WiFi + 音频 + 蓝牙 | 1–3 月 | 固件包、ICNSS 驱动 |
| P3 | Modem 通话/数据 | 3–6 月+ | IPA、Modem 固件、Ofono |
| P4 | 相机/传感器/指纹 | 6 月+ | 多 SKU 处理、闭源固件 |

---

## 6. 推荐技术路线

### 路线 A：下游优先（推荐起步）

1. Fork `device-xiaomi-willow` → `device-xiaomi-ginkgo`
2. Fork `linux-xiaomi-willow` → `linux-xiaomi-ginkgo`（或复用现有 `4.14.117-perf+` 内核树）
3. 修改 DTB/defconfig 为 ginkgo
4. 复用 willow 触控固件与 `ts_nt36xxx` 模块方案
5. 用 pmbootstrap 构建并 fastboot 刷入

| 优点 | 缺点 |
|------|------|
| 最快看到桌面 | 长期维护下游 4.14 内核 |
| 与现有内核经验一致 | 与 pmOS 主线方向不完全一致 |

### 路线 B：Mainline 优先（长期目标）

1. 基于 `linux-postmarketos-qcom-sm6125` + 最新 mainline（含 ginkgo DTS）
2. 创建 `device-xiaomi-ginkgo`，参考 `device-xiaomi-laurel`
3. 逐步 upstream：DSI 面板 → 触控 → WiFi → 音频
4. 向 `~postmarketos/upstreaming` 邮件列表提交 patch

| 优点 | 缺点 |
|------|------|
| 符合 pmOS 长期方向 | 短期只能 serial 控制台 + simplefb |
| 易合并 upstream | 体验差，不适合日常使用 |

### 路线 C：混合策略（实际项目推荐）

```
Phase 1–2:  下游内核打通 boot + 显示 + 触控
Phase 3+:   各子系统逐个切到 mainline 驱动
最终目标:   全 mainline，或「mainline 内核 + 少量 downstream 模块」
```

---

## 7. 分区与刷机信息

### 7.1 分区布局（实测）

设备无 A/B slot（`ro.boot.slot_suffix` 为空），主要分区：

```
boot, bootbak, recovery, system, vendor, userdata, cache,
vbmeta, vbmetabak, dtbo, dtbobak, persist, ...
```

### 7.2 刷机注意事项

- **刷机方式：** fastboot
- **vbmeta：** 需 `--disable-verity --disable-verification`
- **Boot 镜像格式：** 标准 Qualcomm boot.img（pagesize=4096）
- **DTB：** 需 append_dtb（`deviceinfo_append_dtb="true"`）
- **USB ID：** Vendor `0x2717`（Xiaomi）

### 7.3 内核 cmdline 参考（当前 LineageOS）

```
console=ttyMSM0,115200n8
androidboot.console=ttyMSM0
earlycon=msm_serial_dm,0x4a90000
androidboot.hardware=qcom
androidboot.bootdevice=4744000.sdhci
androidboot.usbcontroller=4e00000.dwc3
msm_drm.dsi_display0=dsi_nt36672a_tianma_vid_display
androidboot.fpsensor=gdx
```

---

## 8. 关键资源链接

| 资源 | URL |
|------|-----|
| pmaports 仓库 | https://gitlab.com/postmarketOS/pmaports |
| SM6125 mainline 内核 fork | https://gitlab.com/sm6125-mainline/linux |
| ginkgo 主线 DTS (common) | `arch/arm64/boot/dts/qcom/sm6125-xiaomi-ginkgo-common.dtsi` |
| ginkgo 主线 DTS (device) | `arch/arm64/boot/dts/qcom/sm6125-xiaomi-ginkgo.dts` |
| willow pmOS 设备包 | `pmaports/device/testing/device-xiaomi-willow/` |
| willow 下游内核包 | `pmaports/device/testing/linux-xiaomi-willow/` |
| SM6125 mainline 内核包 | `pmaports/device/testing/linux-postmarketos-qcom-sm6125/` |
| 初始 ginkgo patch | Gabriel Gonzales, LKML 2025-03 |
| 社区邮件列表 | linux-arm-msm@vger.kernel.org |
| pmOS upstream 列表 | ~postmarketos/upstreaming@lists.sr.ht |
| Android 参考 DT | Lineage `android_kernel_xiaomi_sm6125` |
| Android 设备树 | `device_xiaomi_ginkgo` (各 ROM 项目) |
| 触控/固件来源 | `Xiaomi-trinket-dev/vendor_xiaomi_sm6125-common` |
| postmarketOS Wiki (设备) | https://wiki.postmarketos.org/wiki/Xiaomi_Redmi_Note_8_(xiaomi-ginkgo) |
| postmarketOS Wiki (SoC) | https://wiki.postmarketos.org/wiki/Qualcomm_Snapdragon_665_(SM6125) |

---

## 9. 主要贡献者与社区

| 人员 | 贡献 |
|------|------|
| Gabriel Gonzales (semfault) | ginkgo 初始 mainline DTS |
| Barnabás Czémán (barni2000) | ginkgo/willow 共用 dtsi、内存/GPIO 修正、pmOS 贡献者 |
| Biswapriyo Nath | 振动/IR/USB-C/RTC/UART 等 patch |
| Eli Riggs (rjeli) | willow 下游 pmOS port |
| Konrad Dybcio | Qualcomm mainline 维护者 |
| Lux Aliaga | laurel (Mi A3) pmOS port、sm6125 内核包 |

---

## 10. 风险与注意事项

1. **内存布局：** 主线早期 reserved memory 定义错误会导致高负载崩溃；验证时使用 `memtest=1` 内核参数。

2. **面板 variant：** 当前设备为 Tianma NT36672A，其他批次可能是 Huaxing FT8719 等，需准备多 DT overlay。

3. **相机 SKU：** 主摄 GM1/GM2/IMX582 混用，需按 hardware revision 分支处理。

4. **vbmeta 校验：** 刷 pmOS 通常需禁用 verity/verification，否则无法启动自定义内核。

5. **无 A/B slot：** 刷机失败可直接进 fastboot/edl 恢复，但需谨慎操作 boot 分区。

6. **Modem 适配：** 完整蜂窝功能是最大难点，许多 pmOS 设备长期无 Modem 支持。

7. **闭源固件：** WiFi/BT/相机/Modem 均依赖 Qualcomm/Xiaomi 专有固件，需从 Android 系统或 vendor 仓库提取。

---

## 11. 建议的下一步行动

1. **搭建 pmbootstrap 环境**，clone pmaports 到本地
2. **Fork willow 设备包**，创建 `device-xiaomi-ginkgo` 初版
3. **对接现有 4.14.117 内核树**，或先用 `linux-xiaomi-willow` 验证 boot
4. **提取触控固件**（novatek_ts_tianma_fw.bin）并测试 `ts_nt36xxx` 模块
5. **在 Wiki 注册/更新** ginkgo 功能表，与 upstream 社区对齐
6. **并行跟踪 mainline**，将显示/触控 patch 往主线提交

---

## 12. 专项调研：主线 Linux + Ubuntu 26.04 + 触控 + WiFi

> 本节针对更新后的项目目标做难度评估。  
> 注：「Ubuntu 26.4」按 **Ubuntu 26.04 LTS (Resolute Raccoon)** 理解，该版本搭载 **Linux 7.0** 内核。

### 12.1 目标拆解

| 子目标 | 含义 | 是否已有基础 |
|--------|------|-------------|
| **主线 Linux 引导** | Linux 7.0 + `sm6125-xiaomi-ginkgo.dtb` 启动 | ✅ 已有（simplefb + USB + eMMC） |
| **Ubuntu 26.04 运行** | arm64 rootfs + systemd + 桌面/网络管理 | ⚠️ 官方无手机镜像，需自行打包 |
| **触控可用** | 手指操作 UI（Wayland/X11 input 设备） | ❌ 主线 ginkgo DTS 无触控节点 |
| **WiFi 可用** | 连接无线网络 | ❌ SM6125 主线 SoC DTS 尚无 WiFi 节点 |

### 12.2 总体难度评估

| 综合评级 | **高难度（7.5 / 10）** |
|----------|------------------------|
| 乐观估计 | 熟练开发者 **全职 3–4 个月** 可达「Ubuntu + 显示 + 触控 + WiFi 基本可用」 |
| 保守估计 | **6–9 个月**（含 upstream 等待、固件调试、面板时序排查） |
| 最大风险 | **WiFi** — SM6125 平台主线 WiFi 基础设施尚未完备 |

**关键结论：** 触控和 WiFi 不能孤立实现——Ubuntu 桌面要「正常使用」，还依赖 **DRM 显示** 先跑通。实际依赖链为：

```
主线 boot → DRM 显示 → 触控输入 → WiFi 网络 → Ubuntu 桌面体验
                ↑                      ↑
           当前最大短板            平台级缺失
```

### 12.3 分项难度详评

#### A. 主线 Linux 引导 + Ubuntu 26.04 — 难度：★★★☆☆（中等）

**现状：**
- ginkgo 已在主线有 `sm6125-xiaomi-ginkgo.dts`，可启动到 serial console
- Ubuntu 26.04 LTS 将 arm64 纳入主 archive，自带 Linux 7.0
- 官方 Ubuntu arm64 面向服务器 / 骁龙笔记本 / 边缘设备，**不提供** Redmi Note 8 开箱镜像

**需要做的：**
1. 编译/打包 Linux 7.0 + ginkgo DTB（或基于 Ubuntu 内核源码打 patch）
2. 构建 arm64 rootfs（`debootstrap` / `mmdebstrap` + Ubuntu 26.04 源）
3. 制作 boot.img（kernel + initramfs）+ 刷入 userdata/root 分区
4. 处理 vbmeta 禁用、fstab、分区挂载

**预估：** 2–4 周（有嵌入式 Linux 经验）

**风险：** 低–中。引导本身问题不大；Ubuntu 桌面在 simplefb 上能跑但体验差（无 GPU 加速）。

---

#### B. DRM 显示（触控和桌面的前置条件）— 难度：★★★★☆（较高）

**你的设备：** Tianma **NT36672A** 1080×2340 DSI 面板（cmdline 已确认）

**主线现状：**

| 项目 | 状态 |
|------|------|
| SM6125 MDSS/DSI 控制器 | ✅ `sm6125.dtsi` 已有 `mdss_dsi0` 节点 |
| Novatek NT36672A 面板驱动 | ⚠️ 已有 `panel-novatek-nt36672a.c`，但仅支持 **1080×2246**（Poco F1 Tianma），**非** ginkgo 的 1080×2340 |
| ginkgo DTS 中 MDSS/面板 | ❌ `ginkgo-common.dtsi` 仅有 simple-framebuffer，无 `&mdss_dsi0` |
| 参考实现 | laurel（Samsung S6E8FC0）、seine（Samsung SOFEF01）已有完整 MDSS 配置 |

**需要做的：**
1. 从下游 DTS / 下游内核提取 ginkgo 面板 power sequence、GPIO、pinctrl、regulator
2. 为 1080×2340 添加新 panel compatible 或扩展现有 `panel-novatek-nt36672a` 的 timing/init 序列
3. 在 `sm6125-xiaomi-ginkgo-common.dtsi` 启用 `&mdss_dsi0`、`&mdss_dsi0_phy` 及面板子节点
4. 验证 DRM/KMS 输出（`modetest`、Weston/GNOME）

**预估：** 4–8 周（含 upstream patch 往返）

**风险：** 中–高。面板 init 序列错误会导致花屏/不亮；不同批次面板（Tianma vs Huaxing）可能需要多个 DT overlay。

---

#### C. 触控 — 难度：★★★★☆（较高）

**你的设备：** Novatek **NT36672A** 触控（与面板同系列 IC，I2C 独立通信）

**主线现状：**

| 项目 | 状态 |
|------|------|
| 主线驱动 `novatek-nvt-ts` | ✅ 存在，支持 `novatek,nt36672a-ts` 及 variant |
| ginkgo DTS 触控节点 | ❌ 未添加 |
| willow pmOS 方案 | 使用**下游** `ts_nt36xxx` 模块 + `novatek_ts_tianma_fw.bin` 固件，**非主线** |
| laurel 主线参考 | 使用 `focaltech,ft3518`（完全不同硬件） |

**需要做的：**
1. 从下游内核/DTS 确定 I2C 总线、地址（下游 FW 地址 `0x01`）、中断 GPIO、reset GPIO
2. 在 ginkgo DTS 添加 touchscreen 节点（`novatek,nt36672a-ts` 或 variant）
3. 确认是否需要触控固件（下游有 bin 固件；主线 `novatek-nvt-ts` 通常不需要）
4. 调试 wake_type / chip_id variant（NT36672A 有 e7t 等变体，参数不匹配会 probe 失败 -5）
5. 在 Ubuntu 中验证 `/dev/input/event*` + libinput

**预估：** 3–6 周（可与显示并行，但依赖显示先亮）

**风险：** 中–高。下游 `ts_nt36xxx` 与主线 `novatek-nvt-ts` 是不同驱动栈；可能需要多次迭代 compatible / GPIO / power supply。

---

#### D. WiFi — 难度：★★★★★（很高）

**你的设备：** Qualcomm **WCN3990**（`qcom,icnss` / `ath10k_snoc`）

**主线现状（关键发现）：**

| 项目 | 状态 |
|------|------|
| `sm6125.dtsi` 中 WiFi 节点 | ❌ **Linux 7.0 仍无 `wifi` 节点**（`sm6115.dtsi` 有，sm6125 未移植） |
| ginkgo / laurel / seine DTS | ❌ 均无 `&wifi { status = "okay"; }` |
| ath10k SNOC 驱动 | ✅ 主线有 `ath10k_snoc` + `wcn36xx` |
| SM6125 设备 WiFi 跑通先例 | ❌ **尚无公开成功案例** |
| 参考 | sm6115 平台 j606f、fxtec-pro1x 已启用 WiFi；qcm2290 有 `firmware-name` override |

**需要做的（工作量大）：**
1. **SoC 级：** 向 `sm6125.dtsi` 添加 `wifi@c800000` 节点（参考 sm6115，含 memory-region、interrupts、iommus）
2. **设备级：** 在 ginkgo DTS 添加 `&wifi` 配置（4 路 regulator + `qcom,calibration-variant`）
3. **固件：** 从手机 `/vendor/firmware_mnt/image/` 提取：
   - `wlanmdsp.mbn`
   - `bdwlan.bin` → 转换为 `board-2.bin`
   - `firmware-5.bin`（需与 wlanmdsp 版本匹配）
4. 安装到 `/lib/firmware/ath10k/WCN3990/hw1.0/`
5. 调试 ath10k probe、校准 variant 命名、可能的 WCN3990 A-MSDU 分片 bug（主线 2026 年仍在修，吞吐量可能差）
6. 提交 upstream patch 并等待合并

**预估：** 2–4 个月（含 sm6125 SoC WiFi 节点 upstream）

**风险：** 很高。这是整个项目最大的技术风险点。即使驱动加载成功，吞吐量和稳定性也可能需要额外 patch（如 ath10k in-order rx amsdu 持久化 patch，2026 年 7–8 月仍在 RFC 阶段）。

### 12.4 难度矩阵

| 子系统 | 主线就绪度 | 开发工作量 | 技术风险 | 综合难度 |
|--------|-----------|-----------|---------|---------|
| Boot + serial | 90% | 低 | 低 | ★★☆☆☆ |
| Ubuntu rootfs | N/A（打包问题） | 中 | 低 | ★★★☆☆ |
| DRM 显示 | 40% | 高 | 中高 | ★★★★☆ |
| 触控 | 30% | 高 | 中高 | ★★★★☆ |
| WiFi | **10%** | **很高** | **很高** | ★★★★★ |

### 12.5 推荐实施路线（主线专用）

与此前「下游优先」不同，既然目标明确为**主线 Linux**，推荐：

```
阶段 1（2–4 周）  主线 boot + Ubuntu minimal rootfs + serial 登录
阶段 2（4–8 周）  DRM 显示（NT36672A 1080×2340 面板驱动 + ginkgo DTS）
阶段 3（3–6 周）  触控（novatek-nvt-ts + ginkgo DTS，与阶段 2 部分并行）
阶段 4（2–4 月）  WiFi（sm6125.dtsi wifi 节点 upstream + 固件 + ginkgo 校准）
阶段 5（可选）    Ubuntu Desktop (GNOME/Wayland) 整体联调
```

**阶段 1 即可验证「Ubuntu 能启动」**，但用户体验仅 serial console。  
**阶段 2+3 达成「触控桌面可用」。**  
**阶段 4 达成完整目标。**

### 12.6 与参考设备的差距

| 设备 | SoC | 主线显示 | 主线触控 | 主线 WiFi | 备注 |
|------|-----|---------|---------|----------|------|
| **ginkgo（你的）** | SM6125 | ❌ | ❌ | ❌ | 仅 simplefb |
| laurel (Mi A3) | SM6125 | ✅ 进行中 | ✅ ft3518 | ❌ | 面板/触控不同于 ginkgo |
| seine (Xperia 10 II) | SM6125 | ✅ | 部分 | ❌ | 不同面板/触控 |
| j606f (Lenovo P11) | SM6115 | — | — | ✅ | WiFi 参考，同 PM6125 |
| willow (8T) pmOS | SM6125 | ✅ 下游 | ✅ 下游 | ❌ | 非主线方案 |

ginkgo 的 WiFi/触控/显示均不能直接从 laurel 或 willow 复制，需按 ginkgo 硬件单独适配。

### 12.7 Ubuntu 26.04 特别说明

- Ubuntu 26.04 LTS 默认内核 **Linux 7.0**，与 ginkgo 主线 DTS 进展基本同步
- arm64 已进主 archive，但 **没有** 针对 SM6125 手机的官方支持
- 骁龙 X Elite 笔记本跑 Ubuntu 26.04 仍面临固件提取、GPU 回归等问题（Phoronix 2026 评测），手机平台只会更复杂
- 实际路径：自维护 kernel package + 自定义 rootfs，而非等待 Canonical 官方支持
- 可考虑先用 **postmarketOS** 或 minimal Ubuntu rootfs 验证驱动，再迁移到完整 Ubuntu Desktop

### 12.8 最终判断

| 问题 | 回答 |
|------|------|
| 目标是否可行？ | **可行**，但是长期工程，不是短期 hack |
| 最大瓶颈？ | **WiFi**（SM6125 SoC 主线 WiFi 节点缺失 + 固件 + 无先例） |
| 触控 alone 能否先做？ | 可以，但无显示时只能 serial 验证 input 事件；完整体验依赖 DRM |
| 比 postmarketOS 下游方案更难吗？ | **显著更难**。willow 下游方案 1–2 月可出桌面；主线方案周期 ×2–3 |
| 是否建议坚持主线？ | 若目标是长期维护 + upstream 贡献 → **值得**；若目标是快速出成果 → 下游更快 |

---

## 附录 A：主线 ginkgo DTS 结构

当前 mainline 设备树文件：

```
sm6125-xiaomi-ginkgo.dts          # 设备入口（12 行，include common）
sm6125-xiaomi-ginkgo-common.dtsi  # 共用部分（ginkgo + willow）
sm6125-xiaomi-willow.dts          # willow 特有（NFC 等）
sm6125.dtsi                       # SoC 级定义
pm6125.dtsi                       # PMIC 定义
```

## 附录 B：willow deviceinfo 关键字段参考

```bash
deviceinfo_codename="xiaomi-willow"
deviceinfo_dtb="qcom/sm6125-xiaomi-willow"   # ginkgo 应为 sm6125-xiaomi-ginkgo
deviceinfo_screen_width="1080"
deviceinfo_screen_height="2340"
deviceinfo_no_framebuffer="true"
deviceinfo_flash_method="fastboot"
deviceinfo_generate_bootimg="true"
deviceinfo_bootimg_qcdt="false"
deviceinfo_append_dtb="true"                 # laurel 使用，ginkgo 可能也需要
deviceinfo_flash_pagesize="4096"
```

## 附录 C：项目仓库现状

- 路径：`.`
- 状态：空目录，尚未开始 port 工作
- 已有资产：设备上运行的自编译内核 `4.14.117-perf+`（编译者 `local-builder@host`）

---

*本文档由 postmarketOS ginkgo 适配调研自动生成，后续 port 进展请在此文档或单独 changelog 中更新。*
