**语言：** [English](../ginkgo-dsi-err-status5-analysis.md) | 简体中文

# ginkgo 显示黑屏与 `dsi_err status=5` 分析记录

> 设备：Redmi Note 8 (ginkgo) · SM6125 · Tianma NT36672A 1080×2340  
> 关联日志：`backup/ginkgo/logs/uart-20260808-225704.log`（build #45）  
> 最后更新：2026-08-08（本文冻结为 **build #45 阶段** 的历史分析）  
>
> **后续：2026-08-17 显示已出图。** `status=5` 启动瞬态不再挡出图。完整经历见 [ginkgo-display-complete-2026-08-17.md](./ginkgo-display-complete-2026-08-17.md)。

本文档记录 **有背光、无图像** 阶段对 `dsi_err_worker: status=5` 的解码、社区经验、安卓下游备份对照。总览见 [ginkgo-mainline-bringup-chronicle.md](./ginkgo-mainline-bringup-chronicle.md)。

---

## 1. 当时现象（build #45，已过时）

| 阶段 | 背光 | 画面 | 说明 |
|------|------|------|------|
| UEFI 启动 | — | ✅ logo | `DisplayDxe: tianma panel 1080x2340` |
| 内核接管瞬间 | — | 闪一下 | UEFI 画面 → DRM 重初始化 |
| 进系统后 | ✅ | ❌ 黑屏 | systemd `gpio-backlight` 打开 PMI632 GPIO6 |

**关键日志（`uart-20260808-225704.log`）：**

```
panel-tianma-nt36672a: panel init complete
panel-tianma-nt36672a: Skipping enable of already enabled panel
fb0: sys_fillrect: framebuffer is not in virtual address space
dsi_err_worker: status=5          # ×5，仅启动时
msm_dpu: [drm] fb0: msmdrmfb frame buffer device
arm-smmu: Unhandled context fault: iova=0x5c003000   # ×11
```

**已确认：**

- DCS 命令通道正常（`panel init complete`）
- 无 `vblank timeout`（build #43 之后已消失）
- build #45 已注入下游 PHY timing blob + PHY `vdda` 改 0.9V（`vreg_l7a`），**`status=5` 仍在**

---

## 2. `status=5` 含义

定义见主线 `linux/drivers/gpu/drm/msm/dsi/dsi_host.c`：

| 位 | 宏 | 含义 |
|----|-----|------|
| `0x1` | `DSI_ERR_STATE_TIMEOUT` | `REG_DSI_TIMEOUT_STATUS` 非零（HS/LP/BTA 超时） |
| `0x2` | `DSI_ERR_STATE_DLN0_PHY` | PHY lane 0 错误（本次 **未出现**） |
| `0x4` | `DSI_ERR_STATE_FIFO` | `REG_DSI_FIFO_STATUS` 非零（lane FIFO 溢出/下溢） |
| `0x8` | `DSI_ERR_STATE_MDP_FIFO_UNDERFLOW` | MDP 侧 FIFO 下溢（本次 **未出现**） |

**`status=5` = `0x1 | 0x4` = TIMEOUT + FIFO**

中断处理路径：`dsi_host_irq` → `dsi_error()` → 读 `FIFO_STATUS` / `TIMEOUT_STATUS` 等 → `dsi_err_worker`。

### 与其他常见 status 对比（社区）

| status | 组合 | 常见场景 |
|--------|------|----------|
| `4` | 仅 FIFO | hdisplay / pclk 与面板不匹配；DSC 宽度算错 |
| `5` | TIMEOUT + FIFO | 视频引擎开启后数据流不同步（**本次**） |
| `c` | PHY + FIFO | command mode 重入；PHY 时序严重错误 |

下游 `reference/downstream/drivers/dsi-staging/dsi_ctrl_hw.h` 中对应概念：

- `DSI_HS_TX_TIMEOUT` — 高速正向发送超时
- `DSI_DLNx_HS_FIFO_UNDERFLOW/OVERFLOW` — 各 data lane FIFO 异常

---

## 3. 时间线还原（UART）

```
UEFI DisplayDxe
  ├─ tianma 1080×2340 初始化
  └─ logo @ (388, 123)                    → 用户可见「闪一下」

~1.44s  display-subsystem → IOMMU group 2
~1.46s  IOMMU fault @ 0x5c003000 ×11      → 仍访问 UEFI 帧缓冲区域

~2.50s  [drm] Initialized msm 1.13.0
~2.66s  low vbp+vfp may lead to perf issues
~2.75s  panel init complete               → DCS OK
~2.87s  dsi_err status=5 ×5               → 视频流启动失败
~3.15s  fb0: msmdrmfb registered

~10s+   systemd-backlight 打开背光        → 有背光、无像素
```

**结论分层：**

1. **UEFI 显示链路正常** — 面板硬件、bootloader 初始化无问题
2. **DCS 通道正常** — 面板已 `display on`
3. **视频 HS 像素流异常** — `status=5` 的直接原因
4. **背光与图像独立** — GPIO 背光晚于 DRM 由 userspace 打开

---

## 4. 社区经验与参考链接

### 4.1 DCS 必须在 PHY PLL 锁定之后

- 来源：[sm8150-linux-mainline#1](https://github.com/sm8150-linux-mainline/linux/issues/1)
- 问题：`prepare()` 里发 DCS → `-22` 或假初始化
- 修复：`prepare()` 仅上电/复位，`enable()` 发 DCS
- **ginkgo 状态：** 已修复（build #44 调整 bridge 顺序），日志有 `panel init complete`

### 4.2 DSI 软复位需足够延时

- 来源：[LKML AUTOSEL 4.9 — drm/msm/dsi reset](https://lists.freedesktop.org/archives/dri-devel/2019-October/241751.html)
- 修复：`dsi_sw_reset` 中 `msleep(20)` 替代 `wmb()`
- **ginkgo 状态：** 主线已有 `DSI_RESET_TOGGLE_DELAY_MS 20`

### 4.3 MDP 与 DSI 同时灌流导致 FIFO

- 来源：[Freedreno — pp done timeout + status=c](https://lists.freedesktop.org/archives/dri-devel/2019-November/243717.html)
- 问题：command mode 下 autorefresh 与显式 commit 叠加
- **ginkgo：** video mode，不太像本条，但说明 FIFO 常与上游送帧时序有关

### 4.4 hdisplay / pclk / DSC 宽度不一致 → status=4

- 来源：[LKML — DPU DSC INTF timing](https://www.spinics.net/lists/kernel/msg6110040.html)
- **ginkgo：** 无 DSC，但 DPU INTF 与 DSI 寄存器宽度仍须一致；日志有 `low vbp+vfp` 警告

### 4.5 SM6125 主线显示已在他机跑通

- 来源：[msm8953-mainline/linux](https://github.com/msm8953-mainline/linux) — SM6125 MDSS/DPU/DSI 合入主线
- Xperia 10 II (Seine) 等机型有完整 panel 配置
- **含义：** 框架可用，ginkgo 问题在 **板级/面板/UEFI 交接**

---

## 5. 安卓备份资料怎么用

### 5.1 目录索引

见仓库 `reference/README.md`：

```
reference/downstream/
├── dts/xiaomi/ginkgo/
│   ├── display/dsi-panel-nt36672a-tianma-fhd-video.dtsi  # 面板时序 + DCS init
│   └── ginkgo-trinket-display.dtsi                       # PHY timing blob
├── dts/qcom/trinket-sde.dtsi                             # PHY 节点、clamp、供电
└── drivers/dsi-staging/                                  # 完整 enable 链逻辑
```

`backup/ginkgo/` 主要为 boot 镜像与 UART 日志，**显示逆向以 `reference/downstream/` 为准**。

### 5.2 下游完整上电顺序（`dsi_display.c`）

```
时钟 / PHY 上电
  → phy reset
  → disable clamp（释放 UEFI PHY freeze）     ← 主线缺失
  → toggle resync FIFO（v3/v4 PHY；ginkgo v2.0 无）
  → panel DCS enable
  → video engine enable（dsi_display_vid_engine_enable）
```

主线（`dsi_manager.c` build #44+）：

```
pre_enable:  DSI host power on
atomic_enable: panel bridge enable (DCS) → msm_dsi_host_enable (视频)
```

### 5.3 下游 vs 主线差异表

| 项目 | 下游 (trinket / ginkgo) | 主线 (当前) | 状态 |
|------|-------------------------|---------------|------|
| 面板模式 | `non_burst_sync_event` | 无 BURST/SYNC_PULSE flag → 同 | ✅ |
| `tx-eot-append` | DT 显式开启 | 默认开启 EOT（无 `NO_EOT`） | ✅ |
| `t-clk-pre/post` | `0x37` / `0x0f` | `qcom,dsi-clk-pre/post` 已配 | ✅ |
| PHY timing blob | 40 字节 per-lane | build #45 DT `qcom,dsi-phy-lane-timings` | ✅ 已加，err 仍在 |
| PHY `vdda-0p9` | VDD_MX (pm6125 L7) | build #45 `vreg_l7a` + regulator | ✅ 已加，err 仍在 |
| strength / lane-config | `[ff 06]` / `[00 00 10 0f]` | 14nm 硬编码，可能不完全一致 | ⚠️ 待对齐 |
| **PHY clamp** | `phy_clamp_base @ 0x5e01400` | **未映射、无驱动** | ❌ 高优先级 |
| DSI host `vdda-1p2` | L18A | `vreg_l18a` on `mdss_dsi0` | ✅ |
| `lp11-init` | DT 有 | 主线未见 | ⚠️ 待查 |
| cont splash 清理 | `dsi_display_splash_res_cleanup` | 无 | ⚠️ 与 IOMMU fault 相关 |
| resync FIFO | v3/v4 only | 14nm-2290 无 | N/A (v2.0 PHY) |

### 5.4 PHY clamp（高优先级缺口）

下游 `dsi_phy_hw_v2_0_clamp_ctrl()`（`dsi_phy_hw_v2_0.c`）：

- 寄存器基址：`0x5e01400`（`DSI_MDP_ULPS_CLAMP_ENABLE_OFF`）
- 作用：UEFI `DisplayDxe` 初始化后释放 lane freeze
- 下游在 `dsi_display_set_clamp(false)` 中于 HS 时钟建立前调用

主线 `sm6125.dtsi` 中 `mdss_dsi0_phy` 仅映射：

- `0x5e94400` dsi_phy
- `0x5e94500` dsi_phy_lane
- `0x5e94800` dsi_pll

**缺少 `0x5e01400` clamp 区域** — 与「闪一下后黑屏」及 IOMMU @ `0x5c003000` 高度相关。

### 5.5 面板 DT 关键属性（`dsi-panel-nt36672a-tianma-fhd-video.dtsi`）

```dts
qcom,mdss-dsi-traffic-mode = "non_burst_sync_event";
qcom,mdss-dsi-tx-eot-append;
qcom,mdss-dsi-bllp-eof-power-mode;
qcom,mdss-dsi-bllp-power-mode;
qcom,mdss-dsi-lp11-init;
qcom,mdss-dsi-reset-sequence = <1 10>, <0 10>, <1 10>;
```

PHY timing（`ginkgo-trinket-display.dtsi`）：

```
[26 21 09 0b 06 02 04 a0] ×4 data lanes
[26 20 0a 0b 06 02 04 a0]   clk lane
```

---

## 6. 次要问题（非黑屏主因，但需记录）

### 6.1 `fb0: framebuffer is not in virtual address space`

- MSM GEM 帧缓冲常为 DMA-only，无 CPU vmap
- fbdev 控制台无法 `fillrect` / `imageblit`
- **DPU 硬件 scanout 不依赖 CPU 映射**；若 DSI 正常，仍应有画面
- 影响：fbcon 无法画字，不是 `status=5` 根因

### 6.2 `Skipping enable of already enabled panel`

- `drm_panel_enable()` 被调用两次，第二次跳过
- bridge / atomic 提交链冗余，非致命

### 6.3 IOMMU fault @ `0x5c003000`

- 位于 UEFI 帧缓冲区（`~0x5c000000`）
- display-subsystem 加入 IOMMU 后，仍有访问旧 FB 地址
- 可能与未清理的 UEFI 显示状态或未释放 clamp 有关

---

## 7. 已尝试修复（build 时间线）

| Build | 改动 | `dsi_err` | 画面 |
|-------|------|-----------|------|
| #42 | SM6125 DSI 6g v2.9 时钟、`clk-pre/post` | status=4/5 | 黑 |
| #43 | DCS 移到 `enable()` | 改善，无 vblank timeout | 黑 |
| #44 | atomic_enable：先 panel 再 `host_enable` | status=5 | 黑 |
| #45 | PHY timing blob、PHY 0.9V、`dsi_tpg` debugfs | **仍 status=5** | 闪一下后黑 |

---

## 8. 修复优先级（建议）

| 优先级 | 方向 | 依据 |
|--------|------|------|
| **P0** | 实现 **PHY clamp 释放**（DTS `0x5e01400` + `dsi_phy_14nm` clamp_ctrl） | 下游 enable 链；解释闪一下 + 黑屏 |
| **P0** | **DSI TPG** 验证（见下） | 区分 DSI 物理层 vs DPU |
| **P1** | 读 `TIMEOUT_STATUS` / `FIFO_STATUS` 具体位 | 精确定位 HS_TX 超时还是 FIFO underflow |
| **P1** | 对齐下游 **strength / lane-config** | `trinket-sde.dtsi` |
| **P2** | DPU tearcheck / `qcom,te-source`、byte_intf 时钟 | `low vbp+vfp` 警告 |
| **P2** | 处理 IOMMU @ `0x5c000000` / cont splash 清理 | 避免 DPU 指向 UEFI FB |

---

## 9. 调试命令

### 9.1 SSH 连网

```bash
# USB 网卡名每次可能变化
sudo ip addr add 192.168.7.1/24 dev enxXXXXXXXXXXXX
ssh root@192.168.7.2
```

### 9.2 错误与 DRM 状态

```bash
dmesg | grep -E 'dsi_err|panel init|vblank|iommu.*5c00'
cat /sys/kernel/debug/dri/0/kms | head -100
cat /sys/kernel/debug/clk/clk_summary | grep -i dsi
```

### 9.3 DSI 测试图（build #45+）

在显示已 enable 后：

```bash
echo 1 > /sys/kernel/debug/dri/0/dsi_tpg
```

| 结果 | 含义 |
|------|------|
| 出现棋盘格 | DSI + 面板 OK → 查 DPU / plane / CRTC |
| 仍黑屏 | 继续查 PHY / 时钟 / clamp |

### 9.4 UART 采集

```bash
sudo python3 scripts/uart-monitor.py
# 日志：backup/ginkgo/logs/uart-YYYYMMDD-HHMMSS.log
```

---

## 10. 相关源文件（主线）

| 用途 | 路径 |
|------|------|
| 板级 DTS | `linux/arch/arm64/boot/dts/qcom/sm6125-xiaomi-ginkgo.dts` |
| SoC DTS | `linux/arch/arm64/boot/dts/qcom/sm6125.dtsi` |
| 面板驱动 | `linux/drivers/gpu/drm/panel/panel-novatek-nt36672a.c` |
| DSI manager | `linux/drivers/gpu/drm/msm/dsi/dsi_manager.c` |
| DSI host / err | `linux/drivers/gpu/drm/msm/dsi/dsi_host.c` |
| 14nm PHY | `linux/drivers/gpu/drm/msm/dsi/phy/dsi_phy_14nm.c` |
| DISPCC | `linux/drivers/clk/qcom/dispcc-sm6125.c` |
| 下游参考 | `reference/downstream/drivers/dsi-staging/` |
| 下游 DTS | `reference/downstream/dts/xiaomi/ginkgo/` |

---

## 11. 变更记录

| 日期 | 内容 |
|------|------|
| 2026-08-08 | build #46：PHY clamp 释放（`0x5e01400`）、DSI FIFO/timeout 寄存器日志 |
