**语言：** [English](../ginkgo-display-complete-2026-08-17.md) | 简体中文

# Redmi Note 8 (ginkgo) 主线显示：从「有背光无图像」到品红上屏

> 设备：Xiaomi Redmi Note 8 · codename **ginkgo** · SoC **SM6125 (trinket)** · 序列号 `<serial>`  
> 面板：Tianma **NT36672A** · 1080×2340@60 · 4-lane MIPI DSI **Video** · `non_burst_sync_event`  
> 背光：Kinetic **KTD3136** @ I2C `0x36`，HWEN = PMI632 GPIO6  
> 栈：**主线 DRM / DPU 5.4 / DSI 6G v2.3 / 14nm PHY**，**不回退 simplefb**  
> 验收日：2026-08-17 · 用户确认屏幕「有点粉色」（品红 framebuffer 测试图）  
> 本文是这次 **DPU→DSI 出图** 的完整经历：误区、决定性实验、代码、寄存器、心得。

**关联文档**

| 文档 | 内容 |
|------|------|
| [ginkgo-display-bringup-methodology.md](./ginkgo-display-bringup-methodology.md) | 更早阶段：LCDB / DCS / PHY / DPMS |
| [ginkgo-fbcon-boot-2026-08-18.md](./ginkgo-fbcon-boot-2026-08-18.md) | 内核启动日志上屏（fbcon / `console=tty0`） |
| [ginkgo-dsi-err-status5-analysis.md](./ginkgo-dsi-err-status5-analysis.md) | `dsi_err status=5` 专项 |
| [ginkgo-mainline-bringup-chronicle.md](./ginkgo-mainline-bringup-chronicle.md) | 全机 bring-up 时间线 |
| [thinking.md](../thinking.md) | 更早的寄存器原始笔记 |
| [display-bringup-loop.sh](../scripts/display-bringup-loop.sh) | Agent 调试 SOP |
| [usb-connect.sh](../scripts/usb-connect.sh) | 每次 reboot 后重配 RNDIS |
| [reboot-fastboot.sh](../scripts/reboot-fastboot.sh) | 从 Ubuntu 进 fastboot |

---

## 0. 一句话结论

屏幕最终能出图，不是因为「再加一点 MDP 时钟」或「再 cycle 一次 DSI」，而是因为：

1. **DSI 自己发像素（TPG）早就通了** —— 面板、PHY、HS、整宽 1080 都没问题。  
2. **DPU INTF 往 DSI MDP FIFO 送像素时一直在打架** —— FIFO 粘在 `0xcccc1019`（VIDEO_MDP 同时 overflow+underflow）。  
3. 打架的开关是 **INTF programmable fetch（VFP 预取）**：catalog 默认 24 行，面板 VFP 只有 10 行。预取窗口落在 DSI 的 BLLP（不吃 MDP 像素）上，第一个 VFP 就把 FIFO 打爆。  
4. **关掉 INTF_1 的 prog fetch**，并且 **先 DSI HS-cycle（INTF 关着）、再开 timing engine** 之后：`LANE=0x1f00`、`FIFO=0x1010`、INTF 60fps、无 `dsi_err`。品红 fb 用户可见。

---

## 1. 心得（给下一次 SoC / 面板 bring-up）

### 1.1 把链路拆成「能独立证伪」的几段

这条显示链至少有五段，每一段都有自己的「铁证」，不要用下一段的现象给上一段定罪：

```
背光 (KTD3136)  ≠  面板 DCS/LP  ≠  DSI HS/PHY  ≠  DPU INTF 像素  ≠  fb/DPMS 内容
```

| 段 | 假阳性 | 真证伪 |
|----|--------|--------|
| 背光 | 亮了以为有图 | I2C id、brightness sysfs |
| DCS | `panel init complete` 以为视频通 | 只证明 LP 命令 |
| DSI HS | `INTF_FRAME_COUNT` 60fps 以为有图 | timing engine 不吃像素也会跑 |
| DPU | underrun=0 以为 INTF 喂饱了 | DSI 掉 HS 时 underrun 反而接近 0 |
| 内容 | fb 全黑 + 链路通 = 用户仍报黑屏 | 写品红/白，且 **FIFO 必须先健康** |

这次最值钱的实验是 **DSI TPG**：在 DSI 控制器内部造像素，完全旁路 DPU。棋盘格/纯绿一旦上屏，就可以把「面板坏了 / lane map 错了 / 缺列」全部关掉，问题收缩到 **INTF→DSI MDP 输入**。

### 1.2 寄存器比日志诚实，但要会解码「粘滞位」

`FIFO_STATUS` 的 overflow/underflow 是 **sticky**。读到 `0xcccc1019` 只说明「曾经同时溢出和欠载」，不一定是这一微秒的瞬时状态。正确用法：

1. 记下当前值  
2. W1C（把读到的值写回去）  
3. 等 1～2 帧再读  

若 W1C 之后立刻又是 `0xcccc`，才是 **持续失配**。若变成 `0x1010`，说明只是启动瞬态。

`LANE_STATUS` 同样：视频模式在 BLLP 里 lane 会回 LP-11，单次读到 `0x1f1f` 可能只是消隐。要连续采样，或结合 `CLK_STATUS` 的 `VID_PCLK_ACTIVE`。

### 1.3 主线默认值在「这一颗 SoC + 这一块面板」上不必成立

主线 DPU catalog 里 `prog_fetch_lines_worst_case = 24` 对很多旗舰屏（VFP 很大）是对的。ginkgo 这块 Tianma 的 VFP=10。公式会变成「整段 VFP 都拿去预取」。INTF 以为自己在帮 MDP 抢时间，DSI 以为这段时间是 BLLP、不该有 MDP 像素 —— 两边都没错，合在一起就错。

同类陷阱：

- `_dpu_core_perf_calc_clk()` 曾用 `hdisplay` 而不是 `mode->clock`，算出 160 MHz < pclk 183 MHz。  
- `clk_inefficiency_factor=220` 让 `mode_valid` 算出 402.6 MHz > 400 MHz 上限，**把 1080x2340 从连接器 mode 列表里删掉**，表现为「没有 fb0、DSI 时钟全 0」。不是显示坏了，是 **mode 被策略拒绝**。  
- 14nm PHY **没有** HS-request 发生器，`LANE_CTRL` bit24 必须为 1（controller 发 HS）。主线对「有 PHY HS req」的芯片会清 bit24。  
- DSI 6G 寄存器相对 xml **+4**。少加 4 会把 `LANE_CTRL` 读成 `LANE_STATUS`。

### 1.4 一次只改一个变量；「顺手再 cycle 一次」会制造新根因

这次走弯路最多的，都是把两个问题叠在一起：

- 为了稳住 HS，在 INTF **已经在喷像素** 时对 DSI 做 **20ms SOFT_RESET**。HS 稳住了，FIFO 被永久打成 `0xcccc`。  
- 为了让 VID_PCLK 不 gating，把 `DYNAMIC_FORCE_ON` 留在 `CLK_CTRL` 上。约 0.5s 后 HS 掉线。  
- 为了清 INTF underrun，把 inefficiency factor 拉到 220，超过 `max_core_clk`，modeset 整体消失。

能复现的 live poke（`/dev/mem`）先验证，再写进内核。内核里「开机自动 TPG」只能当实验，不能当产品路径。

### 1.5 不要让用户当示波器

在 FIFO 还是 `0xcccc` / `0x5555` 的时候请用户看屏，得到的永远是「B：有背光全黑」。等 `LANE=0x1f00` 且 FIFO 稳住 `0x1010`，再写品红请人看 —— 一次就能定性。

### 1.6 调试通道本身也是 bring-up 的一部分

每次 `fastboot boot` / reboot 之后：

- 主机 `enx*` 接口名会变，**必须先跑** `./scripts/usb-connect.sh`  
- SSH：`root@192.168.7.2`，密码 `$GINKGO_ROOT_PASSWORD`，主机 `192.168.7.1`  
- 进 fastboot：`./scripts/reboot-fastboot.sh`  
- 验证用 `fastboot boot out/boot.img`，**不要** `fastboot flash boot`，除非明确要求写分区  
- fastboot 卡死：`echo $GINKGO_ROOT_PASSWORD | sudo -S usbreset 18d1:d00d`

**绝对不要** mmap 超出 INTF_1 块（基址 `0x5e6b800`，长度 `0x2c0`）。读 `INTF+0x6a8` 会把 SoC/USB 卡死。INTF mmap 必须从页对齐的 `0x5e6b000` 开始，但只访问 `+0x800`～`+0xAC0`。

### 1.7 失败实验也要记下来（避免下一次再做）

见第 8 节。重复「关 DATA_HCTL / 开 widebus / 写 INTF TPG / CTL FLUSH」不会产生新信息，只会掉 HS。

---

## 2. 目标、验收、非目标

### 2.1 目标

在主线 Linux 上走完整 **DRM → DPU INTF_1 → DSI0 → 14nm PHY → NT36672A**，用户可见像素。

### 2.2 量化验收（Level B）

| 指标 | 地址 / 来源 | 成功阈值 | 2026-08-17 实测 |
|------|-------------|----------|-----------------|
| DSI HS | `LANE_STATUS` `0x5e940a8` | 以 `0x1f00` 为主，非永久 `0x1f0f`/`0x1f1f` | `0x1f00` 稳定 |
| LANE_CTRL | `0x5e940ac` | bit24=1，bit28=0 | `0x01000000` |
| FIFO | `0x5e9400c` | `0x1010`；无 `0xcccc`/`0x5555` 高 16 位 | `0x1010` 连续数秒 |
| INTF 帧率 | `INTF_FRAME_COUNT` `0x5e6b8ac` | 1s 增量 45–75 | **60** |
| Encoder vsync | `encoder-0/status` | 参考；可低于 60（IRQ 开关） | ~11（IRQ），不影响出图 |
| INTF underrun | 同上 | 非每帧一次 | **0** |
| dmesg | 全程 | 运行期无 `dsi_err` | 启动后 **0** 条 |
| DPMS | `fb0/blank`，`card0-DSI-1/dpms` | `0`，`On` | 是 |
| 用户 | 看屏 | 能认出测试色 | **「有点粉色」** |

`INTF_FRAME_COUNT` 单独 60fps **不能**说明有图像；黑屏时 timing engine 仍可能在跑。  
encoder `frame_done_cnt` 实际是 `frame_done_timeout_cnt`，为 0 表示 **没有 timeout**，不是「没完成帧」。

### 2.3 非目标

- 不回退 simple-framebuffer  
- 不把 Android `dsi-staging` 整棵搬进主线  
- 不在 FIFO 不健康时请用户看屏  
- 不 `fastboot flash boot`（本次全程 `fastboot boot`）

---

## 3. 硬件数据通路

```
  GEM / fb0 (XR24, 1080×2340, stride 4352)
           │
           ▼
  SSPP VIG0 ──▶ LM ──▶ PP ──▶ CTL_0 ──▶ INTF_1 (0x5e6b800)
                                           │  pclk 域，1 pixel / pclk
                                           ▼
                                      DSI Host (0x5e94000, 6G +4)
                                           │  VIDEO_MDP FIFO
                                           │  TPG 可在此旁路 MDP
                                           ▼
                                      14nm PHY (0x5e94400)
                                           │  4-lane HS
                                           ▼
                                      NT36672A 1080×2340
```

时钟（出图时实测 `clk_summary`）：

| 时钟 | 频率 | 说明 |
|------|------|------|
| `disp_cc_mdss_pclk0_clk` | 183012000 | 像素时钟 = htotal×vtotal×60 |
| `disp_cc_mdss_byte0_clk` | 137259000 | bitclk/8 |
| `disp_cc_mdss_byte0_intf_clk` | 68629500 | 14nm：byte/2 |
| `disp_cc_mdss_mdp_clk` | 400000000 | OPP 最高档（factor 218 请求 ~399 MHz） |

面板时序（与下游 dtsi 对齐）：

| | 值 |
|--|----|
| hdisplay / vdisplay | 1080 / 2340 |
| HFP / HPW / HBP | 90 / 2 / 120 → htotal **1292** |
| VFP / VPW / VBP | 10 / 3 / 8 → vtotal **2361**（寄存器里 TOTAL 用 htotal-1 / vtotal-1） |
| pclk | 183.012 MHz |
| 像素格式 | RGB888，4-lane |
| traffic | `non_burst_sync_event` |
| BLLP | `BLLP_POWER_STOP` + `EOF_BLLP_POWER_STOP`（VID_CFG0=`0x9130`） |

DSI vs INTF 水平窗口（已核对，不是 off-by-one）：

| | 寄存器 | 解码 |
|--|--------|------|
| DSI `ACTIVE_H` | `0x04b2007a` | start=122，end=1202 **exclusive** → 1080 |
| INTF `DISPLAY_HCTL` | `0x04b1007a` | start=122，end=1201 **inclusive** → 1080 |
| DSI `TOTAL` | `0x0938050b` | h=1291，v=2360（即 totals−1） |
| INTF `HSYNC_CTL` | `0x050c0002` | period=1292，pulse=2 |

---

## 4. 用户看屏时间线（诚实记录）

请用户看屏只有在「我们自己已经有寄存器结论」之后。几次反馈把问题切成了完全不同的层：

| 代号 | 用户看到的 | 当时寄存器 | 真正含义 |
|------|------------|------------|----------|
| **C** | 完全没背光 | KTD3136 未 probe / HWEN | 背光 IC，与 DSI 无关 |
| **B** | 有背光、画面全黑 | HS 或 FIFO 坏，或 fb 全 0 | 链路或内容 |
| **A** | **DSI TPG 棋盘格**，最右侧一条黑边 | TPG on，`FIFO=0x1010` | 面板整宽 1080 已通；黑边是棋盘格 tile 对不齐 1080 |
| **A′** | 改成 DSI 整屏纯绿后 **整屏都绿** | 同上 | 右侧不是缺列，是 TPG 图案 |
| **最终** | **有点粉色** | TPG **关**，`FIFO=0x1010`，品红 fb | **DPU 像素上屏** |

C 的修复：`linux/drivers/video/backlight/ktd3136.c` + DTS `kinetic,ktd3136` @ `0x36`，`CONFIG_BACKLIGHT_KTD3136=y`。实机 `id=0x18`，亮度 1024/2047。

---

## 5. 阶段回顾：在 FIFO 之前已经修好的层

这些在 2026-08 上旬到中旬完成，细节见 methodology / thinking.md。没有它们，后面的 TPG/FIFO 实验做不了。

| 层 | 问题 | 修复 |
|----|------|------|
| L1 偏压 | PMI632 LCDB | 主线 LCDB regulator |
| L1 PHY | LDO 被写成 `0x3c` | `dsi_14nm_phy_ldo_cntrl()` → standalone `0x1c` |
| L1 PHY | UEFI 后 clamp 未放 | `dsi_phy_clamp` @ `0x5e01400` |
| L2 DCS | host 未就绪就发命令 | manager：先 panel 再 `host_enable` |
| L2 HS | 主线清 `LANE_CTRL` bit24 | 14nm 必须 bit24=1；面板 `CLOCK_NON_CONTINUOUS` 避免 bit28 |
| L2 时钟 | `byte_intf` 未走 `byte0_div` | DT + `dispcc-sm6125` + host `clk_set_rate(byte_intf_div)` |
| L4 DPMS | deferred fbdev `blank=4` | `display-unblank.service`（**不要**在 probe 里 `restore_fbdev_mode`，会卡死） |

到这一步：DCS `power mode 0x9c`、INTF 60fps、背光可开，用户仍可能是 **B**。

---

## 6. 决定性实验：DSI TPG（旁路 DPU）

### 6.1 做法

debugfs `dsi_tpg`（或直接写）：

- `TEST_PATTERN_GEN_CTRL` EN=bit0  
- 棋盘格曾用 `VID_MDSS_GENERAL_PATTERN` + checkered  
- 纯绿：`VIDEO_INIT_VAL=0x00ff00`，`VIDEO_PATTERN_SEL=VID_FIXED`

**必须在 TPG 打开后再做一次 HS cycle**，否则 lane 仍可能停在 STOP。健康组合：

```
LANE=0x1f00   FIFO=0x1010   TPG=EN
```

### 6.2 结果

| 状态 | LANE | FIFO | 用户 |
|------|------|------|------|
| TPG on + HS cycle | `0x1f00` | `0x1010` | 棋盘格 / 整屏绿 |
| TPG off，不 reset | 常 `0x1f1f` | `0x55551011` | 黑（HS FIFO 空） |
| TPG off，再 cycle | `0x1f00` | `0xcccc1019` | 黑（MDP 失配） |

`0xcccc1019` 解码（`dsi.xml` FIFO_STATUS，6G 地址 `0x5e9400c`）：

| 位 | 名 | 含义 |
|----|-----|------|
| 0 | VIDEO_MDP_OVERFLOW | DSI 视频引擎 MDP 输入溢 |
| 3 | VIDEO_MDP_UNDERFLOW | 同时欠 |
| 12 | DLN0_LP_EMPTY | 常与 `0x1010` 一起出现，可视为「空闲/健康」的一部分 |
| 18/19, 22/23, 26/27, 30/31 | 四 lane HS overflow **且** underflow | 打包/节拍在一行之内来回打满打空 |

**结论（当时）：** DSI→面板 OK；DPU INTF→DSI MDP **不同步**。  
**错误结论（后来推翻）：** 「一定是 MDP 欠时钟」或「一定是 1ppc/2ppc 打包」。欠时钟能解释 INTF underrun，**不能**单独解释 TPG 好、DPU 永远 `0xcccc`。

产品路径 **不要**开机自动开 TPG。TPG 只是示波器。

---

## 7. MDP 时钟：真问题，但不是 FIFO 根因

### 7.1 主线算低了

`_dpu_core_perf_calc_clk()` 原先用 `vtotal * hdisplay * vrefresh` → **160 MHz**，低于 INTF pclk 183 MHz。INTF 几乎每帧 underrun。

已改为：

```c
mode_clk = (u64)mode->clock * 1000;  /* 183012000 */
```

再乘 `clk_inefficiency_factor`。factor 105 → 请求 192 MHz → OPP 落到 **256 MHz**，underrun 仍接近每帧一次。debugfs `perf_mode=1` 拉到 **400 MHz** 后 underrun **清零**，但 FIFO 仍是 `0xcccc1019`。

所以：400 MHz 是「INTF 喂得饱」的条件，**不是**「DSI MDP FIFO 健康」的条件。

### 7.2 factor 220 的陷阱（整次 modeset 蒸发）

`dpu_crtc_mode_valid()`：

```c
adjusted = mode->clock * factor / 100;          /* kHz */
if (max_core_clk_rate < adjusted * 1000)
    return MODE_CLOCK_HIGH;
```

SM6125 无 `has_3d_merge`，不除 2。

| factor | 请求 | 结果 |
|--------|------|------|
| 105 | 192 MHz | mode 在，OPP 256 MHz |
| 220 | **402.6 MHz** | **> 400 MHz → mode 被删** → `/sys/class/drm/card0-DSI-1/modes` 空、无 fb0、DSI 时钟 enable_count=0 |
| **218** | 398.97 MHz | mode 在，OPP **400 MHz** |

日志里的 `Cannot find any crtc or sizes` 在 factor 220 时是 **真的没有可用 mode**，不是 fbdev 偶发。

catalog 现为 `clk_inefficiency_factor = 218`。

### 7.3 一个容易误读的相关现象

| 条件 | encoder underrun |
|------|------------------|
| HS 掉线 `0x1f1f`，DSI 不抽像素 | 可以接近 **0**（INTF 自己空转） |
| HS `0x1f00` 但 FIFO `0xcccc` | **每帧 underrun**（DSI 在错误节拍抽像素） |
| FIFO `0x1010` + 400 MHz + 无 prog fetch | **0** |

不要用「underrun=0」反推 FIFO 健康。

---

## 8. 试过、无效、不要再做的实验

下列 live poke 或内核改动 **没有**把 DPU 路径的 FIFO 修到 `0x1010`。重复它们只会掉 HS 或浪费一次 `fastboot boot`。

| 实验 | 结果 |
|------|------|
| 关 `DATA_HCTL_EN`（INTF_CFG2=0） | FIFO 仍坏，HS 更容易掉；已从 `dpu_hw_intf.c` 撤回特殊例外 |
| INTF 1ms / 20ms off/on | HS → `0x1f1f` |
| INTF off → DSI cycle → INTF on（**仍开着 prog fetch**） | 立刻 `LANE=0x1f00 FIFO=0xcccc` |
| `INTF_MUX` 写成 0 | 无帮助（`0xf0000` 低位=PP0 是预期） |
| `PANEL_FMT=0x213f` | 读回仍 `0x2100`（bpc 位被 HW 丢掉） |
| DSI `DATABUS_WIDEN` bit25 | 6G v2.3 **写不进去** |
| 只开 INTF widebus | 更差 |
| INTF TPG 寄存器 | 此 SoC 未接，写了读回 0 |
| VID 去掉 BLLP power stop（`0x0130`） | HS 都拉不起来 |
| CTL `FLUSH=0xffffffff` / 乱 `START` | HS 掉线 |
| `DISP_INTF_SEL` 写成 DSI | DPU 5.x 基本 NOP |
| 成功后再 HS cycle 一次（settle） | 常把已成功的 `0x1f00` 打回 STOP，或把 `0x1010` 打成 `0xcccc` |
| `DYNAMIC_FORCE_ON` 留在 CLK_CTRL | ~0.5s HS 掉，`CLK_STATUS` 失去 DYN_PCLK/BYTECLK |
| 1µs SOFT_RESET 代替 20ms | 瞬时 `0x1f00`，FIFO 很快 `0xcccc`，HS 不稳 |
| 关 BLLP / 改 traffic mode | 未改善 MDP FIFO |

**mmap 红线：** 不要读 `INTF_1+0x6a8`。INTF_STATUS 是 `0x26C`，FRAME_COUNT 是 `0x0AC`。DSI 用 `mmap(..., 0x1000, offset=0x5e94000)` 小窗口。

---

## 9. 真正根因：INTF programmable fetch × DSI BLLP

### 9.1 机制

`dpu_encoder_phys_vid.c` 里 `programmable_fetch_get_num_lines()`：

- catalog `prog_fetch_lines_worst_case = 24`  
- SOF 可吸收行数 = VBP + VPW = 8+3 = **11**  
- 还缺 13 行，但 VFP 只有 **10**，于是 **整段 VFP 都标成 fetch 窗口**  
- `INTF_CONFIG` bit31 = `PROG_FETCH_START_EN`，实测 `0x80800000`

DSI 视频模式在 VFP 里走 **BLLP**（`VID_CFG0=0x9130` 含 BLLP_POWER_STOP）：这段时间 **不从 MDP FIFO 取像素**，只发 blanking 包。

INTF 预取若把「下一帧像素」提前推到 INTF→DSI 的 CDC/FIFO 上：

- VFP：DSI 不取 → **VIDEO_MDP overflow**  
- 下一帧 active：FIFO 已被搅乱 → **underflow**，lane HS FIFO 同时 overflow+underflow → `0xcccc1019`  
- 时间上：INTF 刚 enable 时 FIFO 可以是 `0x1010`（还没碰到第一个 VFP），**约 16～21ms 后**（第一帧结束）变成 `0xcccc`

这与 1ms 采样完全吻合：

```
normal CFG 0x80800000
   0ms  LANE 0x1f00 FIFO 0x1010
  21ms  LANE 0x1f00 FIFO 0xcccc1019
```

TPG 不走 MDP FIFO，所以 TPG 永远 `0x1010`。

### 9.2 决定性 live poke

INTF 关掉，清 `INTF_CONFIG` bit31（CFG 变成 `0x800000`），DSI 20ms cycle，再开 INTF：

```
noprog CFG 0x800000
   0ms  …
   1ms  LANE 0x1f00 FIFO 0x1010
   （40ms 内不再变化）
```

拉长到 2s：FIFO 保持 `0x1010`。W1C 之后仍是 `0x1010`。

只清 bit23、保留 bit31：第一个帧周期就坏。  
两个都清：FIFO 不像 `0xcccc`，但 HS 在 STOP/HS 之间抖（bit23 不要随便动）。

### 9.3 代码修复

`linux/drivers/gpu/drm/msm/disp/dpu1/catalog/dpu_5_4_sm6125.h` **只改 INTF_1（DSI）**：

```c
.prog_fetch_lines_worst_case = 0,
```

INTF_0（DP）仍为 24。ginkgo 不用 DP。

关掉预取之后，VBP+VPW=11 行要够 INTF 从内存取第一行。MDP 已跑在 400 MHz（约 2.2× pclk），实测 **underrun=0**。

若以后换一块 VFP 很大的屏，可以再打开预取；**不要**在 VFP=10 的 NT36672A 上用 24。

---

## 10. 启动顺序：INTF 关着做 DSI HS-cycle

### 10.1 错误顺序（FIFO 永久 0xcccc）

```
enable_timing(1)          // INTF 开始喷像素
host_enable()
  op_mode_config ENABLE
  dsi_ginkgo_hs_cycle()   // 20ms SOFT_RESET，INTF 仍在喷
  [可选] 再 cycle 一次     // settle，经常更糟
```

14nm 上第一次 `DSI_EN` 常把 data+clk 留在 STOP（`0x1f1f`）。ENABLE 翻转 + `HS_REQ_SEL_PHY` + `DYNAMIC_FORCE_ON` + SOFT_RESET 能把 lane 拉到 `0x1f00`。  
但 **INTF 正在跑的时候 reset 20ms** ≈ 往死掉的 MDP FIFO 里倒 1.2 帧像素。

### 10.2 正确顺序（与 live 实验一致）

```
host_enable()             // INTF 仍为 0
  hs_cycle ×N             // 此时 LANE 常为 0x1f1f，FIFO 0x11111010，正常
enable_timing(1)          // 立刻 LANE 0x1f00、FIFO 0x1010（无 prog fetch 时能保持）
```

`dpu_encoder_kickoff()` 现为：先 `msm_dsi_manager_host_enable()`，再 `handle_post_kickoff()`（内部 `enable_timing(1)`）。

开机日志会出现：

```
ginkgo DSI after EN tries=9 ... LANE_ST=0x1f1f FIFO=0x11111010 CLK_ST=0x4343
```

`tries=9` 是 INTF 还没开，8 次 cycle 都拉不起 HS，**预期如此**。随后 timing 一开，采样就是 `0x1f00` / `0x1010`。不要为了在 host_enable 里看到 `0x1f00` 再把 INTF 提前打开。

### 10.3 `dsi_ginkgo_hs_cycle()` 要点

- `DSI_EN` 拉低 → `CLK_CTRL |= DYNAMIC_FORCE_ON` → bit24=1、清 DLN force stop  
- `SOFT_RESET` 保持 **20ms**（1µs 不够稳 HS）  
- **必须把 CLK_CTRL 写回原值**（通常 `0x23f`），不能把 DYNAMIC_FORCE_ON 留下  
- 再 `DSI_EN=1`，再等 ~20ms  

视频模式的 `dsi_err_worker` **不要**再 `dsi_sw_reset`（会把正在出图的 FIFO 打烂），只清 sticky。

---

## 11. 代码改动清单（出图相关）

路径均相对 `linux/`，仅列与这次出图直接相关的。更早的 LCDB/clamp/LDO 见 methodology。

| 文件 | 改动 | 为什么 |
|------|------|--------|
| `drivers/gpu/drm/msm/disp/dpu1/catalog/dpu_5_4_sm6125.h` | INTF_1 `prog_fetch_lines_worst_case=0` | **FIFO 根因** |
| 同上 | `clk_inefficiency_factor=218` | 400 MHz OPP，且不超过 `mode_valid` |
| `drivers/gpu/drm/msm/disp/dpu1/dpu_core_perf.c` | `mode_clk = mode->clock * 1000` | 不要用 hdisplay 低估 pclk |
| `drivers/gpu/drm/msm/disp/dpu1/dpu_encoder.c` | kickoff：先 DSI host_enable，后 `enable_timing` | 避免 INTF 喷着时 20ms reset |
| `drivers/gpu/drm/msm/dsi/dsi_host.c` | `LANE_CTRL` bit24=1；`dsi_ginkgo_hs_cycle()`；kickoff 后最多 8 次 ENABLE 重试；视频 err 不 `sw_reset` | 14nm HS |
| `drivers/video/backlight/ktd3136.c` + DTS | KTD3136 | 用户症状 C |
| rootfs-overlay `display-unblank.service` | 开机 unblank fb0 | 避免 `blank=4` 假黑屏 |

构建：

```bash
./scripts/build-kernel.sh && ./scripts/build-bootimg.sh
./scripts/reboot-fastboot.sh
fastboot boot out/boot.img
```

---

## 12. 寄存器速查（ginkgo 实机，DSI 6G = xml+4）

| 项 | 物理地址 | 出图时 |
|----|----------|--------|
| DSI CTRL | `0x5e94004` | `0x1f3` |
| FIFO_STATUS | `0x5e9400c` | **`0x1010`** |
| VID_CFG0 | `0x5e94010` | `0x9130` |
| LANE_STATUS | `0x5e940a8` | **`0x1f00`** |
| LANE_CTRL | `0x5e940ac` | `0x01000000` |
| CLK_CTRL | `0x5e9411c` | `0x23f`（不要留 `0x200b3f`） |
| CLK_STATUS | `0x5e94120` | `0x9047c3`（含 VID_PCLK） |
| TPG CTRL | `0x5e9415c` | `0x4`（关） |
| INTF_1 TIMING_EN | `0x5e6b800` | `1` |
| INTF_CONFIG | `0x5e6b804` | **`0x800000`**（bit31 预取 **关**；旧值 `0x80800000` 会炸 FIFO） |
| INTF_HSYNC_CTL | `0x5e6b808` | `0x50c0002` |
| INTF_DISPLAY_HCTL | `0x5e6b83c` | `0x4b1007a` |
| INTF_CONFIG2 | `0x5e6b860` | `0x10`（DATA_HCTL） |
| INTF_DISPLAY_DATA_HCTL | `0x5e6b864` | `0x4b1007a` |
| INTF_PANEL_FORMAT | `0x5e6b890` | `0x2100` |
| INTF_FRAME_COUNT | `0x5e6b8ac` | ~60/s |

采样脚本（只读 DSI，避免 INTF mmap 踩雷）：

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

品红测试（stride 4352，BGRA）：

```python
w,h,stride=1080,2340,4352
px=b"\xff\x00\xff\x00"  # magenta
row=px*w
with open("/dev/fb0","r+b",0) as f:
    for y in range(h):
        f.seek(y*stride); f.write(row)
```

---

## 13. 2026-08-17 验收记录

开机（prog fetch 关 + 新 kickoff 顺序）后 SSH：

```
panel init complete
ginkgo DSI after EN tries=9 ... LANE_ST=0x1f1f FIFO=0x11111010   # INTF 尚未 enable，预期
fb0: msmdrmfb
blank=0  dpms=On
INTF CFG=0x800000 EN=1
LANE=0x1f00 FIFO=0x1010 TPG=0x4 CLKST=0x9047c3
t+1s / t+2s 仍为 0x1f00 / 0x1010
INTF FRAME_COUNT 1s 增量 = 60
encoder underrun = 0
dmesg 中 dsi_err 条数 = 0
mdp_clk = 400000000  pclk0 = 183012000
背光 brightness = 1024
```

写入品红 fb 后用户原话：**「是的 有点粉色的感觉」**。

随后把 `vtcon1`（frame buffer device）bind 回 1，尝试恢复 fbcon/tty1。链路未回退。

---

## 14. 已知遗留（显示已通，不必挡出图）

| 项 | 说明 |
|----|------|
| encoder vsync ~11 Hz | INTF 硬件 60fps；vsync IRQ 可能被动态开关。不影响可见图像 |
| `frame_done_timeout_cnt=0` | 名字像「没完成」，其实是 timeout 计数 |
| 启动 `tries=9` + `LANE_ST=0x1f1f` | host_enable 时 INTF 未开，可忽略 |
| `lcdb_ncp` sysfs 电压读数 | 旧问题，与出图无关 |
| IOMMU fault `0x5c00xxxx` | UEFI FB 残留 |
| PWM 与 KTD3136 并存策略 | 当前以 KTD3136 为准 |
| 预取关闭的代价 | 极紧 VBP 的模式理论上更吃 MDP 带宽；ginkgo 400 MHz 已够 |

下一步（非显示/触控阻塞）：WiFi、把 `fastboot boot` 固化进 boot 分区（需用户明确要求）等。

触控全记录：[ginkgo-touch-complete-2026-08-17.md](./ginkgo-touch-complete-2026-08-17.md)。

---

## 15. 给 Agent 的 SOP（显示回归）

1. `./scripts/usb-connect.sh`  
2. 看 `dmesg`：`panel init complete`、允许 host_enable 时 `LANE_ST=0x1f1f`  
3. `blank`/`dpms`、`clk_summary` 的 pclk/mdp  
4. 读 `LANE`/`FIFO`，隔 1s 再读；需要 60fps 时读 `INTF+0x0AC`（注意 mmap 范围）  
5. FIFO 不是 `0x1010`：**不要请用户看屏**；先查 `INTF_CONFIG` bit31 是否又被打开  
6. FIFO 健康后再写品红/白，再问用户  

---

## 附录：FIFO_STATUS 常见值

| 值 | 含义 |
|----|------|
| `0x1010` | 健康（TPG 或 DPU 像素均应如此） |
| `0x11111010` | 四 lane HS empty + LP empty；INTF 未开时常见 |
| `0x55551011` / `0x55551019` | HS FIFO 空，常伴随 STOP |
| `0xcccc1019` | MDP 同时溢/欠 + 四 lane HS 同时溢/欠 → 预取/双引擎失配 |
| `0xdddd1011` | 类似，HS 已掉到 STOP |
| `0x44441019` | 以 overflow 为主的过渡态 |

---

*记录结束。出图日期 2026-08-17。*
