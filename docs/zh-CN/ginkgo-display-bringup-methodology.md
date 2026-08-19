**语言：** [English](../ginkgo-display-bringup-methodology.md) | 简体中文

# Redmi Note 8 (ginkgo) 主线显示适配：完整思路与修复记录

> 设备：Xiaomi Redmi Note 8 · codename **ginkgo** · SoC **SM6125**  
> 面板：Tianma **NT36672A** · 1080×2340 · 4-lane MIPI DSI Video Mode  
> 文档目的：系统总结「有背光、无图像」黑屏问题的**整体修复思路**、分层诊断方法、已验证根因与代码改动。  
> 最后更新：2026-08-17（**品红 framebuffer 用户可见，整条 DRM/DPU/DSI 链路已通**）

**出图完整经历（FIFO / TPG / INTF prefetch / 启动顺序）：**  
[ginkgo-display-complete-2026-08-17.md](./ginkgo-display-complete-2026-08-17.md)

**关联文档：**

| 文档 | 内容 |
|------|------|
| [ginkgo-display-complete-2026-08-17.md](./ginkgo-display-complete-2026-08-17.md) | **2026-08-17 出图全记录与心得** |
| [ginkgo-touch-complete-2026-08-17.md](./ginkgo-touch-complete-2026-08-17.md) | **2026-08-17 触控出点全记录** |
| [ginkgo-mainline-bringup-chronicle.md](./ginkgo-mainline-bringup-chronicle.md) | 全机 bring-up 时间线 |
| [ginkgo-dsi-err-status5-analysis.md](./ginkgo-dsi-err-status5-analysis.md) | 早期 `dsi_err status=5` 专项（已过时阶段） |
| [thinking.md](../thinking.md) | 寄存器级逆向与实验笔记（原始记录） |
| [display-bringup-loop.sh](../scripts/display-bringup-loop.sh) | **Agent 调试 Skill**（SOP + 决策树） |
| [reference/downstream/](../reference/downstream/) | LineageOS / 高通下游显示参考 |

---

## 目录

1. [问题定义与目标](#1-问题定义与目标)
2. [显示子系统架构](#2-显示子系统架构)
3. [症状演进时间线](#3-症状演进时间线)
4. [整体修复思路（分层模型）](#4-整体修复思路分层模型)
5. [阶段一：让 DRM 栈跑起来](#5-阶段一让-drm-栈跑起来)
6. [阶段二：打通电源与 DCS 命令通道](#6-阶段二打通电源与-dcs-命令通道)
7. [阶段三：DSI 高速视频链路（核心难点）](#7-阶段三dsi-高速视频链路核心难点)
8. [阶段四：软件层 DPMS / fbdev 黑屏](#8-阶段四软件层-dpms--fbdev-黑屏)
9. [寄存器级诊断方法（/dev/mem）](#9-寄存器级诊断方法devmem)
10. [已实施代码改动清单](#10-已实施代码改动清单)
11. [下游 vs 主线差异对照表](#11-下游-vs-主线差异对照表)
12. [当前状态与已知遗留](#12-当前状态与已知遗留)
13. [推荐调试流程（SOP）](#13-推荐调试流程sop)
14. [经验教训与原则](#14-经验教训与原则)

---

## 1. 问题定义与目标

### 1.1 用户可见症状

| 阶段 | UEFI Logo | 内核接管后 | 背光 | 图像 |
|------|-----------|------------|------|------|
| 早期 | ✅ | 全黑 | ❌ | ❌ |
| 中期 | ✅ 闪一下 | 黑屏 | ✅ | ❌ |
| 修复 PHY / DCS 后 | ✅ 闪一下 | 黑屏或白屏（视 DPMS） | ✅ | 链路未稳定 |
| **2026-08-17** | ✅ | **品红测试图可见** | ✅ KTD3136 | **已出图** |

**目标：** 在主线 Linux 上走完整 **DRM/MDSS/DPU/DSI** 路径出图，**不回退 simple-framebuffer**。

### 1.2 非目标（刻意不做）

- 不在内核里保留 Android 下游 `dsi-staging` 整套框架
- 不用 `simple-framebuffer` 占位（无法验证真实 DSI 链路）
- 不依赖屏幕进行日常调试（SSH + UART + `/dev/mem` 为主）

### 1.3 核心判断标准

显示「真正修好」需同时满足：

1. **DCS 双向通信正常** — `power mode readback: 0x9c`（与下游 ESD 检查一致）
2. **DSI data lane 进入 HS** — `LANE_STATUS` 的 STOPSTATE 位为 0
3. **无持续性 `dsi_err`** — 启动瞬态 FIFO 错误可接受，运行期不应刷屏
4. **DPMS / CRTC 开启** — `fb0/blank = 0`，connector `dpms = On`
5. **用户可见像素** — fbcon、全白测试或桌面

---

## 2. 显示子系统架构

### 2.1 硬件数据通路

```
┌─────────┐    ┌─────┐    ┌─────────┐    ┌──────────┐    ┌─────────────┐
│  DPU    │───▶│ CTL │───▶│ INTF_1  │───▶│ DSI Host │───▶│ 14nm PHY    │───▶ 面板 NT36672A
│ 5e01000 │    │     │    │ (视频)  │    │ 5e94000  │    │ 5e94400     │     Tianma 1080×2340
└─────────┘    └─────┘    └─────────┘    └──────────┘    └─────────────┘
     ▲                                        │
     │                                        ├── ESC：DCS 命令（LP 模式）
  disp_cc                                   └── HS：RGB 像素流（Video 模式）
  (pclk/byte/esc)
```

### 2.2 软件栈

```
fbcon / userspace
       │
       ▼
  drm_fb_helper (fb0)          ← 可能默认 DPMS OFF（见 §8）
       │
       ▼
  msm_drm (DPU KMS)            ← dpu_kms.c, dpu_encoder_phys_vid.c
       │
       ├── drm_bridge: dsi_mgr  ← dsi_manager.c（panel DCS → host enable 顺序）
       └── drm_panel: nt36672a  ← panel-novatek-nt36672a.c
                │
                ▼
           msm_dsi_host          ← dsi_host.c（LANE_CTRL bit28 关键）
                │
                ▼
           msm_dsi_phy_14nm      ← dsi_phy_14nm.c（LDO_CNTRL 关键）
```

### 2.3 与 UEFI 的交接

- UEFI `DisplayDxe` 已初始化 Tianma 面板并显示 logo → **硬件与面板本身无问题**
- 内核接管时 UEFI 显示状态残留：
  - PHY 可能处于 **clamp（freeze）** 状态
  - `LANE_CTRL` 可能遗留 `HS_REQ_SEL_PHY`（bit24）
  - IOMMU fault @ `0x5c003000`（访问 UEFI 帧缓冲残留地址）

**结论：** 内核必须按正确顺序 **释放 clamp → 重配 PHY → 重走 panel init → 开视频引擎**，不能假设 UEFI 状态可继承。

### 2.4 背光与图像的独立性

ginkgo 背光由 **两路** 共同决定：

| 层级 | 机制 | 主线实现 |
|------|------|----------|
| 升压使能 | PMI632 **GPIO6** | `gpio-backlight`（`default-on`） |
| 亮度 | 面板 DCS **`0x51`** + **`0x53`** | `ginkgo_tianma_on_cmds_2` 中 `0x51,0xB8` / `0x53,0x2C` |

因此可能出现：

- **有背光、无图像** — GPIO 升压已开，但 DSI 无像素 / 面板未 display-on
- **无背光、有图像** — 理论上可能（DCS 亮度为 0），ginkgo 上较少见

---

## 3. 症状演进时间线

| Build / 阶段 | 关键现象 | 当时结论 |
|--------------|----------|----------|
| 早期 | 无 `/dev/dri`，面板 `-22` | 驱动未编入、PHY 地址错、pinctrl 缺失 |
| LCDB 驱动后 | `panel init complete` 但仍黑 | DCS LP 通道通，视频 HS 未通 |
| build #43+ | `panel init complete`，无 vblank timeout | 时钟大体正常 |
| build #45 | `dsi_err status=5` ×8，有背光无图 | TIMEOUT + FIFO，data lane 卡 LP-11 |
| 寄存器实验后 | 清 bit28 + LDO `0x1c` → `LANE_STATUS=0` | **根因锁定在 PHY/DSI 配置** |
| fb 诊断后 | `fb0/blank=4`（POWERDOWN） | **软件层额外关屏** |

---

## 4. 整体修复思路（分层模型）

我们把黑屏问题拆成 **五层**，自底向上逐层排除：

```
┌────────────────────────────────────────────────────────────┐
│ L5 用户空间   fbcon DPMS、systemd 背光、桌面 compositor     │
├────────────────────────────────────────────────────────────┤
│ L4 软件帧缓冲  fbdev deferred、blank=4、GEM 无 CPU 映射      │
├────────────────────────────────────────────────────────────┤
│ L3 显示管线    DPU modeset、INTF timing、CRTC enable          │
├────────────────────────────────────────────────────────────┤
│ L2 DSI 视频    HS 数据传输、FIFO、LANE_CTRL、traffic mode   │
├────────────────────────────────────────────────────────────┤
│ L1 物理电气    LCDB 偏压、PHY LDO、clamp、PLL、panel reset   │
└────────────────────────────────────────────────────────────┘
```

**原则：**

1. **先证明下层 OK 再查上层** — 不要在没有 `panel init complete` 时调 DPU 时序
2. **用只读实验验证假设** — `/dev/mem` 读 `LANE_STATUS` 比猜驱动逻辑快
3. **对照下游 DTS + 寄存器** — ginkgo 有完整 LineageOS 参考
4. **一次只改一个变量** — 便于和 UART 日志关联
5. **区分「链路通」与「有画面」** — 链路通后还可能被 DPMS 关掉

---

## 5. 阶段一：让 DRM 栈跑起来

### 5.1 问题与修复摘要

| 问题 | 根因 | 修复 |
|------|------|------|
| 无 `/dev/dri` | 面板驱动 `=m` 未加载；fragment 未每次 merge | `CONFIG_DRM_PANEL_NOVATEK_NT36672A=y`；`build-kernel.sh` 强制 merge |
| `msm_dsi_phy 0.phy` | MDSS `#address-cells=1` 导致 PHY 地址解析为 0 | `#address-cells/#size-cells = <2>` |
| PHY 不 probe | compatible 错误 | `qcom,dsi-phy-14nm-2290` |
| `failed to get reset gpio` | 无 `PINCTRL_MSM` | 启用 `CONFIG_PINCTRL_SM6125` |
| TE pinctrl 死锁 | `mdss_te_active` 阻塞 panel probe | 从面板节点移除 TE pinctrl |
| 无 SMMU / ICC | display 无法完成 devlink | `CONFIG_ARM_SMMU`、SM6125 interconnect |

### 5.2 配置要点（`config/ginkgo.fragment`）

```kconfig
CONFIG_DRM_MSM=y
CONFIG_DRM_PANEL_NOVATEK_NT36672A=y
CONFIG_BACKLIGHT_CLASS_DEVICE=y
CONFIG_BACKLIGHT_GPIO=y
CONFIG_PINCTRL_MSM=y
CONFIG_PINCTRL_SM6125=y
CONFIG_REGULATOR_QPNP_LCDB=y
CONFIG_SM_DISPCC_6125=y
```

### 5.3 本阶段验收标准

```bash
ls /dev/dri/card0 /dev/fb0
dmesg | grep "Initialized msm"
ls /sys/bus/mipi-dsi/devices/5e94000.dsi.0/driver
```

---

## 6. 阶段二：打通电源与 DCS 命令通道

### 6.1 LCDB 面板偏压（PMI632）

Tianma NT36672A 需要 **正偏压 VSP + 负偏压 VSN**，由 PMI632 内部 **LCDB** 产生，不是简单 `regulator-fixed`。

**改动：**

- 新增 `linux/drivers/regulator/qcom-qpnp-lcdb-regulator.c`
- `pmi632.dtsi`：`qpnp-lcdb@ec00` → `lcdb_ldo_vreg` / `lcdb_ncp_vreg`
- `sm6125-xiaomi-ginkgo.dts`：
  - `vddpos-supply = <&lcdb_ldo_vreg>`
  - `vddneg-supply = <&lcdb_ncp_vreg>`

**验收：**

```
LCDB: LCDB module successfully registered! lcdb_en=1 ldo_voltage=5500mV ncp_voltage=6000mV
panel-tianma-nt36672a: power mode readback: 0x9c
panel-tianma-nt36672a: panel init complete
```

`0x9c` 与下游 `qcom,mdss-dsi-panel-status-value` 一致 → **DCS 命令通道（LP 模式）正常**。

### 6.2 Panel 初始化顺序（DCS 必须在 DSI host 就绪后）

下游与社区经验（[sm8150-linux-mainline#1](https://github.com/sm8150-linux-mainline/linux/issues/1)）：

- `prepare()`：上电、复位
- `enable()`：发 DCS init（需 DSI link 已建立）

**主线修复（`dsi_manager.c`）：**

在 `dsi_mgr_bridge_atomic_enable()` 中 **先** `panel_bridge` enable（DCS），**再** `msm_dsi_host_enable()`（视频引擎）。

**面板驱动（`panel-novatek-nt36672a.c`）ginkgo 序列：**

1. `on_cmds_1` — 厂商寄存器初始化
2. `mipi_dsi_dcs_exit_sleep_mode()` + 80ms
3. `on_cmds_2` — 含 `0x51` 亮度、`0x53` BL 使能
4. `mipi_dsi_dcs_set_display_on()`
5. power mode 读回验证

### 6.3 时序与模式标志（对齐下游）

下游 `dsi-panel-nt36672a-tianma-fhd-video.dtsi`：

```dts
qcom,mdss-dsi-traffic-mode = "non_burst_sync_event";
qcom,mdss-dsi-h-sync-pulse = <0>;
```

主线等价：

```c
.mode_flags = MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_VIDEO
        | MIPI_DSI_CLOCK_NON_CONTINUOUS,
```

- 无 `MIPI_DSI_MODE_VIDEO_BURST` → `dsi_get_traffic_mode()` 返回 `NON_BURST_SYNCH_EVENT` ✅
- `MIPI_DSI_CLOCK_NON_CONTINUOUS` → **禁止** `LANE_CTRL` bit28（见 §7.2）

**显示模式时钟：**

```
htotal = 1080 + 90 + 2 + 120 = 1292
vtotal = 2340 + 10 + 3 + 8  = 2361
clock  = 1292 × 2361 × 60 / 1000 ≈ 183012 kHz
```

---

## 7. 阶段三：DSI 高速视频链路（核心难点）

### 7.1 问题现象

修复 DCS 后仍 **有背光、无图像**，`dmesg` 出现：

```
dsi_err_worker: status=5
msm_dsi: DSI FIFO status: 0xdddd1019
msm_dsi: DSI timeout status: 0x1
```

**`status=5` = `0x1 | 0x4` = TIMEOUT + FIFO** — 视频引擎开启后 data lane 无法正常传 HS 像素。

### 7.2 根因 A：`LANE_CTRL` bit28（CLKLN_HS_FORCE_REQUEST）

**机制：**

`dsi_host.c` 在 `dsi_ctrl_enable()` 中：

```c
if (!(flags & MIPI_DSI_CLOCK_NON_CONTINUOUS)) {
    ...
    dsi_write(REG_DSI_LANE_CTRL,
        lane_ctrl | DSI_LANE_CTRL_CLKLN_HS_FORCE_REQUEST);  // bit28
}
```

- 主线默认：未声明 `NON_CONTINUOUS` 时 **强制 clock lane 连续 HS**
- 下游 ginkgo：**无** `qcom,mdss-dsi-force-clock-lane-hs`，从不置 bit28

**实测（`/dev/mem`）：**

| `LANE_CTRL` bit28 | `LANE_STATUS` | 含义 |
|-------------------|---------------|------|
| 置位（修复前） | `0x1f0f` | 4 条 data lane 永久 STOPSTATE（LP-11） |
| 清除 + 软复位 | `0x1f00` → `0x0` | data lane 进入 HS，FIFO 恢复健康 |

**修复：**

在 `ginkgo_tianma_panel_desc.mode_flags` 增加 `MIPI_DSI_CLOCK_NON_CONTINUOUS`。

### 7.3 根因 B：`PHY LDO_CNTRL` 被覆盖为 `0x3c`

**机制：**

`dsi_14nm_phy_enable()` 对 standalone PHY 正确写入 `0x1c`：

```c
static u32 dsi_14nm_phy_ldo_cntrl(struct msm_dsi_phy *phy)
{
    u32 data = 0x1c;
    if (phy->usecase != MSM_DSI_PHY_STANDALONE)
        data |= DSI_14nm_PHY_CMN_LDO_CNTRL_VREG_CTRL(32);  // → 0x3c
    return data;
}
```

但 `pll_db_commit_14nm()` **曾硬编码** `writel(0x3c, LDO_CNTRL)`，覆盖前述正确值。

- `0x3c` 为 **bonded 双 DSI** 配置，过驱 standalone PHY 的 LDO
- 导致 data lane **无法离开 LP-11**

**修复：**

- 抽取 `dsi_14nm_phy_ldo_cntrl()`，`pll_db_commit_14nm()` 与 `dsi_14nm_phy_enable()` 统一调用

### 7.4 根因 C：PHY clamp 未释放（UEFI 残留）

下游在 HS 建立前调用 `dsi_display_set_clamp(false)`，写 `0x5e01400` 区域。

**修复：**

- `sm6125.dtsi`：`mdss_dsi0_phy` 增加 `dsi_phy_clamp` 寄存器 `0x5e01400`
- `dsi_phy.c`：`msm_dsi_phy_clamp_ctrl(phy, false)` 于 PHY enable 前调用

### 7.5 辅助修复：PHY 时序与供电

| 项目 | 下游 | 主线修复 |
|------|------|----------|
| Lane timing blob | 40 字节 × 5 lane | `qcom,dsi-phy-lane-timings` in ginkgo DTS |
| PHY `vdda` 0.9V | VDD_MX (`vreg_l7a`) | `mdss_dsi0_phy` `vdda-supply` |
| `t-clk-pre/post` | `0x37` / `0x0f` | `qcom,dsi-clk-pre/post` on `mdss_dsi0` |
| Lane CFG0/CFG1 | 下游为 0 | blob 路径补写 `CFG0=0, CFG1=0` |
| `byte0_div` 时钟 | SM6115 有 `@0x20d4` | `dispcc-sm6125.c` 增加 div，`byte0_intf` 挂到 div 输出 |

### 7.6 诊断性日志与工具

- `dsi_host.c`：FIFO / timeout 详细 `drm_err` 日志
- `dpu_kms.c`：debugfs `dsi_tpg`（DSI 测试图案，write-only）
- 用于区分「DPU 没送像素」vs「DSI 发不出去」

### 7.7 本阶段验收标准

```python
# 在设备上通过 /dev/mem 读取（见 §9）
LANE_STATUS (0x5e940a4) == 0x00000000   # 所有 STOPSTATE 位为 0
PHY LDO_CNTRL (0x5e9444c) == 0x0000001c
LANE_CTRL bit28 == 0
```

```bash
dmesg | grep -E 'panel init|dsi_err'   # init OK；dsi_err 仅启动瞬态
```

---

## 8. 阶段四：软件层 DPMS / fbdev 黑屏

### 8.1 问题

在 DSI 链路已修复（`LANE_STATUS=0`）后，仍可能 **无图像**，因：

```
/sys/class/graphics/fb0/blank = 4   # FB_BLANK_POWERDOWN
→ drm_fb_helper_dpms(DRM_MODE_DPMS_OFF)
→ CRTC / encoder 关闭
```

**原因：** `FB_GEN_DEFAULT_DEFERRED_SYSMEM_OPS` 注册的 fbdev 在 **headless 启动**（仅 SSH、无 fb 打开）时保持关屏。

### 8.2 修复思路

| 方案 | 说明 | 状态 |
|------|------|------|
| **userspace** `display-unblank.service` | 启动时 `echo 0 > fb0/blank` | ✅ 已加入 `rootfs-overlay` |
| 内核 `drm_fb_helper_restore_fbdev_mode()` in probe | 过早调用可能导致启动卡死 | ❌ 已回退 |
| 手动测试 | `echo 0 > /sys/class/graphics/fb0/blank` + 写白到 fb0 | ✅ 已验证可触发 DPMS On |

### 8.3 与「背光独立」的关系

即使 DPMS OFF，**gpio-backlight** 仍可能已被 systemd 点亮 → 用户看到「有背光、无图像」。

这容易误判为「DSI 还不通」，实际链路可能已正常。

### 8.4 本阶段验收标准

```bash
cat /sys/class/graphics/fb0/blank      # 0
cat /sys/class/drm/card0-DSI-1/dpms    # On
```

---

## 9. 寄存器级诊断方法（/dev/mem）

### 9.1 前提

- 内核需允许 `/dev/mem` 访问 MMIO（ginkgo 实测可用）
- DSI 控制器物理基址：`0x05e94000`
- PHY 公共寄存器基址：`0x05e94400`
- 主线 `dsi_read()` 相对资源基址 **+4** 偏移（HW_VERSION 在 offset 0）；`/dev/mem` 按 **物理地址** 读即可

### 9.2 关键寄存器

| 物理地址 | 名称 | 健康值（ginkgo） | 说明 |
|----------|------|------------------|------|
| `0x5e94004` | `DSI_CTRL` | `0x1f3` | 使能 + 4 lane |
| `0x5e94008` | `STATUS0` | bit3 `VIDEO_MODE_ENGINE_BUSY` | 视频引擎忙 |
| `0x5e9400c` | `FIFO_STATUS` | 非 `0x55551019` | FIFO 错误位 |
| `0x5e940a4` | `LANE_STATUS` | **`0x0`** | STOPSTATE 全 0 = lane 在 HS |
| `0x5e940a8` | `LANE_CTRL` | bit28=0, bit24=0 | 无强制的连续时钟 HS |
| `0x5e9444c` | `PHY LDO_CNTRL` | **`0x1c`** | standalone 正确 LDO |
| `0x5e94158` | `TEST_PATTERN_GEN_CTRL` | TPG 实验用 | debugfs `dsi_tpg` 写入 |

### 9.3 快速采样脚本（SSH 在设备上执行）

```python
import mmap, os, struct, time

def rd(addr):
    fd = os.open("/dev/mem", os.O_RDONLY | os.O_SYNC)
    m = mmap.mmap(fd, 4096, mmap.MAP_SHARED, mmap.PROT_READ, offset=addr & ~0xfff)
    v = struct.unpack_from("<I", m, addr & 0xfff)[0]
    m.close(); os.close(fd)
    return v

DSI = 0x5e94000
PHY = 0x5e94400

print("LANE_STATUS", hex(rd(DSI + 0xa4)))
print("LANE_CTRL  ", hex(rd(DSI + 0xa8)))
print("FIFO       ", hex(rd(DSI + 0x0c)))
print("LDO_CNTRL  ", hex(rd(PHY + 0x4c)))

# 连续采样 LANE_STATUS
hist = {}
for _ in range(100):
    v = rd(DSI + 0xa4)
    hist[v] = hist.get(v, 0) + 1
    time.sleep(0.002)
print("LANE_STATUS hist", {hex(k): v for k, v in hist.items()})
```

### 9.4 实验：验证 bit28 假设

1. 读 `LANE_CTRL` @ `0x5e940a8`
2. 写 `LANE_CTRL & ~BIT(28)`（需 `O_RDWR`）
3. DSI 软复位
4. 再采样 `LANE_STATUS`

若 STOPSTATE 从全 1 变为 0 → 证实 bit28 为根因。

---

## 10. 已实施代码改动清单

### 10.1 设备树

| 文件 | 改动 |
|------|------|
| `sm6125-xiaomi-ginkgo.dts` | 面板节点、LCDB 供电、PHY timing blob、`vdda`、`dsi-clk-pre/post` |
| `sm6125.dtsi` | MDSS 地址单元、PHY compatible、**`dsi_phy_clamp`** |
| `pmi632.dtsi` | LCDB 稳压器节点 |

### 10.2 面板驱动

| 文件 | 改动 |
|------|------|
| `panel-novatek-nt36672a.c` | ginkgo 专用 init 序列、`MIPI_DSI_CLOCK_NON_CONTINUOUS`、`prepare_prev_first`、enable 中 DCS 顺序 |

### 10.3 DSI / PHY

| 文件 | 改动 |
|------|------|
| `dsi_host.c` | FIFO/timeout 日志；`NON_CONTINUOUS` 时不置 bit28 |
| `dsi_phy_14nm.c` | `dsi_14nm_phy_ldo_cntrl()`；PLL commit 不再硬编码 `0x3c`；CFG0/CFG1=0 |
| `dsi_phy.c` / `dsi.h` | `msm_dsi_phy_clamp_ctrl()` |
| `dsi_manager.c` | atomic_enable：先 panel DCS，再 host_enable；enable 前 clamp off |

### 10.4 时钟

| 文件 | 改动 |
|------|------|
| `dispcc-sm6125.c` | 增加 `byte0_div_clk_src` @ `0x20d4`，`byte0_intf` 挂到 div |
| `qcom,dispcc-sm6125.h` | `DISP_CC_MDSS_BYTE0_DIV_CLK_SRC` 索引 |

### 10.5 稳压器

| 文件 | 改动 |
|------|------|
| `qcom-qpnp-lcdb-regulator.c` | **新增**，PMI632 LCDB |

### 10.6 用户空间

| 文件 | 改动 |
|------|------|
| `rootfs-overlay/usr/local/sbin/display-unblank.sh` | 开机 unblank fb0 |
| `rootfs-overlay/.../display-unblank.service` | systemd 单元 |

### 10.7 调试

| 文件 | 改动 |
|------|------|
| `dpu_kms.c` | debugfs `dsi_tpg` |

---

## 11. 下游 vs 主线差异对照表

| 项目 | 下游 ginkgo | 主线（修复后） | 状态 |
|------|-------------|----------------|------|
| Traffic mode | `non_burst_sync_event` | 默认 NON_BURST_SYNCH_EVENT | ✅ |
| Clock lane HS 强制 | 无 | `NON_CONTINUOUS` 不置 bit28 | ✅ |
| PHY LDO | 固定 `0x1c` | `dsi_14nm_phy_ldo_cntrl()` | ✅ |
| PHY clamp | `0x5e01400` | `dsi_phy_clamp` + driver | ✅ |
| PHY timing blob | 40B × 5 | DTS `qcom,dsi-phy-lane-timings` | ✅ |
| PHY vdda 0.9V | VDD_MX | `vreg_l7a` | ✅ |
| Panel DCS 顺序 | sleep out → init → display on | `panel_enable()` 同等逻辑 | ✅ |
| 背光 | DCS + PWM + GPIO6 | GPIO6 only（DCS 在 init） | ⚠️ 无 PWM 调光 |
| `byte0_div` | 有 | `dispcc-sm6125` 已补 | ✅ |
| TE pin | 有 pinctrl | 已移除（避免死锁） | ✅ |
| fbdev DPMS | Android SurfaceFlinger | 需 `display-unblank` | ⚠️ userspace |

---

## 12. 当前状态与已知遗留

### 12.1 已解决（含 2026-08-17 出图）

- DRM/DSI 驱动 probe 与绑定
- LCDB 偏压、panel DCS init、power mode 读回
- DSI data lane HS（出图时稳定 `LANE_STATUS=0x1f00`；早期文档写的 `0x0` 是误读/消隐采样）
- PHY LDO `0x1c`、clamp 释放、timing blob
- DPMS OFF 导致关屏的定位与 userspace 缓解
- KTD3136 背光（I2C `0x36`，HWEN = PMI632 GPIO6）
- DSI TPG 证明面板整宽 1080 已通
- INTF_1 `prog_fetch_lines_worst_case=0`（VFP=10 时 24 行预取会把 FIFO 打成 `0xcccc1019`）
- MDP 时钟用 `mode->clock * 1000`，`clk_inefficiency_factor=218`（**不要用 220**，会 MODE_CLOCK_HIGH 丢掉全部 mode）
- kickoff：先 `host_enable`（INTF 关着做 20ms HS cycle），再 `enable_timing(1)`
- **用户确认品红 framebuffer 可见**

细节与心得见 [ginkgo-display-complete-2026-08-17.md](./ginkgo-display-complete-2026-08-17.md)。

### 12.2 遗留（不挡出图）

| 项目 | 说明 |
|------|------|
| encoder vsync ~11 Hz | INTF `FRAME_COUNT` 仍 60fps；IRQ 被动态开关 |
| 启动瞬态 `dsi_err` / `tries=9 LANE_ST=0x1f1f` | host_enable 时 INTF 未开，预期如此 |
| `lcdb_ncp` sysfs 读数 12.4V | 可能为驱动 get_voltage 报告问题 |
| IOMMU fault `0x5c00xxxx` | UEFI FB 残留访问 |
| `fb0: not in virtual address space` | 影响 fbcon 画字，不影响硬件 scanout |
| PWM 背光 | 当前以 KTD3136 为准，未复刻下游 PWM 那套 |
| 内核 probe 内 restore fbdev | 曾导致启动卡住，已回退 |

### 12.3 不推荐的做法

- 在 probe 阶段强制 `drm_fb_helper_restore_fbdev_mode_unlocked()`（ginkgo 实测卡死）
- 回退 simple-framebuffer（掩盖 DSI 问题）
- 无条件置 `LANE_CTRL` bit28（ginkgo Tianma 面板不适用）
- `clk_inefficiency_factor=220`（超过 400 MHz 上限，连接器 modes 变空）
- INTF 已 enable 时对 DSI 做 20ms `SOFT_RESET`
- 把 `DYNAMIC_FORCE_ON` 留在 `CLK_CTRL` 上
- mmap 超出 INTF_1 块（读 `INTF+0x6a8` 会卡死 SoC/USB）
- FIFO 仍为 `0xcccc`/`0x5555` 时请用户看屏

---

## 13. 推荐调试流程（SOP）

### 13.1 刷机后首次检查（SSH）

```bash
# 1. 面板与 DRM
dmesg | grep -iE 'panel init|power mode|dsi_err|msm_drm|fb0'

# 2. DPMS
cat /sys/class/graphics/fb0/blank
cat /sys/class/drm/card0-DSI-1/dpms

# 3. 若 blank != 0
echo 0 > /sys/class/graphics/fb0/blank

# 4. FIFO 健康后再写测试色（stride 4352，品红 BGRA）
# 先确认 LANE=0x1f00 FIFO=0x1010，否则不要请用户看屏
python3 -c "
import os
w,h,stride=1080,2340,4352
row=b'\\xff\\x00\\xff\\x00'*w
f=open('/dev/fb0','r+b',0)
for y in range(h): f.seek(y*stride); f.write(row)
print('done')
"
```

### 13.2 若仍黑屏：寄存器层

运行 §9.3 脚本，重点看 `LANE_STATUS`（健康=`0x1f00`）、`FIFO_STATUS`（健康=`0x1010`）、`INTF_CONFIG` bit31（预取应关）、`LDO_CNTRL`。

### 13.3 若 `dsi_err` 持续

```bash
dmesg | grep -iE 'FIFO|timeout|dsi_err'
cat /sys/kernel/debug/clk/clk_summary | grep -iE 'dsi|byte|pclk'
```

对照下游 timing blob 与 `reference/downstream/dts/xiaomi/ginkgo/display/`。

### 13.4 UART 抓取

```bash
sudo python3 scripts/uart-monitor.py
# 日志：backup/ginkgo/logs/uart-*.log
```

---

## 14. 经验教训与原则

### 14.1 关键洞察

1. **「有背光」≠「显示链路通」** — 背光 GPIO / KTD3136 与 DSI 像素独立
2. **「panel init complete」≠「有图像」** — DCS 走 LP，视频走 HS，两层都要通
3. **「LANE_STATUS 全 STOP」是硬件铁证** — 视频模式健康值是 `0x1f00`，不是早期误读的 `0x0`
4. **主线与下游一个 bit 的差异可致命** — bit28、LDO `0x3c` 即如此
5. **软件 DPMS 可制造「假黑屏」** — 链路修好后仍需查 `fb0/blank`
6. **DSI TPG 通 ≠ DPU 通** — TPG 旁路 MDP；`FIFO=0xcccc1019` 是 INTF→DSI 失配
7. **INTF programmable fetch 会和 DSI BLLP 打架** — VFP=10 时不要用 catalog 默认 24 行预取
8. **`dpu_crtc_mode_valid` 会静默丢掉 mode** — factor 220 → 无 fb0，看起来像显示全坏
9. **不要在 INTF 跑着时做 20ms DSI SOFT_RESET** — HS 稳住了，FIFO 被永久打爆
10. **FIFO sticky 位要 W1C 后再采** — 读到 `0xcccc` 只说明曾经溢/欠

### 14.2 修复优先级（经验法则）

```
电源/LCDB → 驱动 probe → DCS → PHY/LANE → 时钟 → DPU INTF/FIFO → DPMS/fbdev → 用户看屏
```

跳过层级会导致误判（例如在 DSI 未 HS 时调 DPU 时序；FIFO 不健康时请用户看屏）。

### 14.3 文档与实验记录

- 每次刷机记录 **build 编号 + UART 日志文件名**
- 寄存器实验写入 `thinking.md` 或本文档附录
- 下游 diff 以 `reference/downstream/` 为准，不以记忆为准

---

## 附录 A：构建与刷机

```bash
cd .
./scripts/build-kernel.sh
./scripts/build-bootimg.sh
# 手机进入 TWRP recovery
FLASH_ROOTFS=0 ./scripts/flash-linux-boot.sh
```

## 附录 B：相关内核日志片段（健康启动）

```
LCDB: LCDB module successfully registered! lcdb_en=1 ldo_voltage=5500mV ncp_voltage=6000mV
[drm] Initialized msm 1.13.0 for 5e01000.display-controller on minor 0
panel-tianma-nt36672a: power mode readback: 0x9c
panel-tianma-nt36672a: panel init complete
msm_dpu: [drm] fb0: msmdrmfb frame buffer device
# dsi_err status=5 仅启动后 ~3.1s 内少量出现，之后无
```

## 附录 C：因果链总图

```
UEFI logo 正常
       │
       ▼
内核 DRM 初始化
       │
       ├── LCDB / reset / clamp ──失败──▶ 无 panel init
       │
       ▼
panel init complete (DCS LP OK)
       │
       ├── LDO 0x3c 或 bit28 ──失败──▶ data lane LP-11, dsi_err status=5
       │
       ▼
LANE_STATUS = 0x1f00 (HS OK)
       │
       ├── fb0 blank=4 (DPMS OFF) ──失败──▶ 有背光、无图像
       │
       ▼
INTF timing + DPU 像素
       │
       ├── INTF PROG_FETCH + 短 VFP ──失败──▶ FIFO 0xcccc，背光亮、画面黑
       │
       ├── kickoff 时 INTF 开着做 DSI 20ms reset ──失败──▶ FIFO 永久 0xcccc
       │
       ▼
FIFO=0x1010 + 品红/fbcon ──▶ 用户可见图像
```

---

*本文档随 bring-up 进展更新。若修复策略或根因结论有变，请同步修改 [ginkgo-mainline-bringup-chronicle.md](./ginkgo-mainline-bringup-chronicle.md) 中的显示章节状态表。*
