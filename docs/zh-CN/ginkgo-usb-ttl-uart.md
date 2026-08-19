**语言：** [English](../ginkgo-usb-ttl-uart.md) | 简体中文

# Redmi Note 8 (ginkgo) Debug UART / USB-to-TTL 详细指南

> 日期：2026-08-05  
> 设备：Xiaomi Redmi Note 8（`ginkgo`，主板型号 LLDM516）  
> 目标：主线 Linux earlycon + serial console（`ttyMSM0` @ 115200）  
> 图片：[`docs/images/uart-ttl/`](./images/uart-ttl/)

---

## 0. 一句话结论

拆开后盖，在主板上找到 **DBG UART 测试点 TP0003(TX) / TP0012(RX)**，用 **1.8V 电平** 的 USB-TTL 交叉接到电脑，波特率 **115200**，即可在刷主线时看到启动日志。

**网上红框的 EDL 测试点不是串口，不要焊错。**

---

## 1. 为什么需要 USB-TTL

当前主线 boot 阶段可能：

- 屏幕还没亮（DRM 未就绪）
- adb / USB gadget 尚未起来
- 内核在 early 阶段就挂了

此时唯一可靠的观测手段是 **SoC Debug UART**。  
项目文档里已写明：

```
console=ttyMSM0,115200n8
earlycon=msm_serial_dm,0x4a90000
```

没有串口线，这些参数等于白写。

---

## 2. 硬件与电气参数

### 2.1 信号映射（权威来源：LLDM516 原理图 + 主线 DTS）

| 项目 | 值 |
|------|-----|
| SoC 外设 | `uart4`，寄存器基址 `0x04a90000`（QUP0 SE4） |
| 引脚 | GPIO16 = TX，GPIO17 = RX |
| 原理图名 | `DBG_UART_TX` / `DBG_UART_RX` |
| 测试点 | **TP0003 = TX**，**TP0012 = RX** |
| Linux 节点 | `ttyMSM0` |
| 波特率 | **115200 8N1**，无硬件流控 |
| 逻辑电平 | **1.8 V**（Qualcomm TLMM 典型值；同类机 lavender 实测 1.8V） |

另有一路非 debug UART（一般不用）：

| TP | 信号 | GPIO |
|----|------|------|
| TP0009 | UART_TX | GPIO14 |
| TP0002 | UART_RX | GPIO15 |

### 2.2 接线表（务必交叉）

| 手机端 | USB-TTL 模块 | 说明 |
|--------|--------------|------|
| TP0003 (`DBG_UART_TX`) | **RX / RXD** | 手机发 → 电脑收 |
| TP0012 (`DBG_UART_RX`) | **TX / TXD** | 电脑发 → 手机收 |
| GND（螺丝孔 / 地焊盘） | **GND** | 必须共地 |
| （无） | **VCC / 3V3 / 5V** | **禁止接到手机** |

示意：

```
PC ──USB──► [USB-TTL，电平拨到 1.8V]
               │ TXD ────────────►  TP0012 (手机 RX)
               │ RXD ◄────────────  TP0003 (手机 TX)
               │ GND ─────────────  GND
               │ VCC （悬空，不接手机）
```

接线总图见：[`images/uart-ttl/ginkgo-usb-ttl-wiring.png`](./images/uart-ttl/ginkgo-usb-ttl-wiring.png)

### 2.3 EDL 点 vs UART 点（极易搞混）

| 类型 | 用途 | 操作 | 能否当串口 |
|------|------|------|------------|
| EDL Test Point | 强制 9008 刷机 | **两焊盘短接** | **否** |
| DBG UART TP0003/TP0012 | serial console | 焊线到 USB-TTL | **是** |

本地 EDL 参考图（仅供拆机方位参考，**不是串口焊点**）：

- `images/uart-ttl/ginkgo-edl-testpoints.jpg`
- `images/uart-ttl/willow-edl-testpoint.jpg`
- `images/uart-ttl/ginkgo-battery-connector.jpg`

公开资料里几乎没有把 `TP0003`/`TP0012` 标在实拍板上的照片。定位方法：

1. 用 **LLDM516 PCB Layout / Boardview** 搜索丝印 `TP0003`、`TP0012`；或  
2. 万用表：关机测导通；上电后 TX 对地约 **1.8V**，开机瞬间有脉冲。

---

## 3. 淘宝 / 拼多多采购清单

下面按「必买 / 强烈建议 / 可选」列出。价格为 2026 年常见区间，下单前以店铺页为准。

### 3.1 必买（核心）

#### A. 1.8V 多电平 USB 转 TTL 模块（主件）

| 项 | 说明 |
|----|------|
| **搜什么** | `FT232 1.8V 3.3V 5V USB转TTL` 或 `FT232RL 多电平 串口模块` |
| **关键规格** | 必须支持 **1.8V** 档（跳线帽或拨码开关可选 1.8/3.3/5） |
| **芯片** | 优先 **FT232RL / FT232RNL**（驱动稳、对 1.8V 支持好） |
| **接口** | Type-C 或 Micro-USB 均可 |
| **参考价** | 约 **15–40 元**；工业级多电平约 **40–80 元** |
| **下单核对** | 商品图上要有 **1.8V** 字样或拨码档位；只有 3.3V/5V 的不要买 |

**推荐搜索关键词（直接复制）：**

```
FT232RL 1.8V 3.3V 5V USB转TTL
FT232 多电平 USB转TTL 拨码
USB转TTL 1.8V 高通 串口
```

**可选品牌/型号方向（非唯一）：**

- 丢石头 / 各类「FT232 多电压」模块（跳线选 1.8V）
- 亿佰特类多电平模块（如支持 1.8/2.5/3.3/5 拨码的 FT232 系列）
- 任意标注「1.8V 可调」的 FT232 小板

**不要买（除非另加电平转换）：**

```
CH340 USB转TTL          ← 多数只有 3.3V/5V
PL2303 USB转TTL         ← 读 1.8V 经常失败
CP2102 3.3V             ← 默认 3.3V，未改 VIO 不能直连
树莓派专用 3.3V 串口线   ← 无 1.8V 档
```

#### B. 杜邦线 / 飞线

| 项 | 说明 |
|----|------|
| **搜什么** | `杜邦线 母对母 20cm` + `杜邦线 公对母` |
| **用途** | 模块排针 ↔ 焊在手机上的飞线转接 |
| **参考价** | **3–8 元** / 排 |

#### C. 漆包线或极细电子线（焊到测试点）

| 项 | 说明 |
|----|------|
| **搜什么** | `漆包线 0.1mm` 或 `电子线 30AWG` / `航模线 30AWG` |
| **用途** | 焊到 TP0003 / TP0012 / GND（测试点很小） |
| **参考价** | **5–15 元** |
| **建议** | 买 **3 种颜色**（红/绿/黑）方便区分 TX/RX/GND |

---

### 3.2 强烈建议（没有会很难焊）

#### D. 电烙铁 + 焊锡 + 助焊剂

| 项 | 搜什么 | 参考价 | 备注 |
|----|--------|--------|------|
| 恒温烙铁 | `恒温电烙铁 60W Type-C` 或 `T12 烙铁` | 40–150 元 | 能稳在 ~300–350℃ |
| 细焊锡丝 | `焊锡丝 0.5mm 含松香` 或 `无铅 0.6mm` | 5–15 元 | **细**比粗好用 |
| 助焊剂 | `助焊膏` / `松香助焊剂笔` | 5–12 元 | 测试点必用 |
| 吸锡/除锡 | `吸锡带` 或 `吸锡器` | 5–15 元 | 焊错可补救 |

手机测试点很脆，**不要用大功率不可控烙铁硬焊**。

#### E. 数字万用表

| 项 | 说明 |
|----|------|
| **搜什么** | `数字万用表`（入门款即可） |
| **用途** | ① 确认 GND；② 测 TX 是否约 1.8V；③ 查短路 |
| **参考价** | **20–60 元** |

#### F. 拆机工具

| 项 | 搜什么 | 参考价 |
|----|--------|--------|
| 撬棒/塑料撬片 | `手机拆机撬棒` | 5–10 元 |
| 吸盘 | `手机屏幕吸盘` | 3–8 元 |
| 十字/五星批 | `手机螺丝刀套装` | 10–25 元 |
| 镊子 | `防静电镊子` | 5–15 元 |

Redmi Note 8 后盖为玻璃胶粘，可搜：`红米 Note 8 拆后盖` 看视频；加热（热风枪/吹风机）更易拆。

---

### 3.3 可选（提升成功率 / 备用方案）

| 零件 | 搜什么 | 何时需要 | 参考价 |
|------|--------|----------|--------|
| 1.8↔3.3 电平转换模块 | `TXS0102 电平转换` 或 `TXB0104 模块` | 手里已有 3.3V USB-TTL 时做兜底 | 5–15 元 |
| 热风枪 | `迷你热风枪 拆机` | 拆后盖/拆屏蔽罩 | 40–100 元 |
| 放大镜/显微镜 | `手机维修显微镜` / `台式放大镜` | 看清 TP 丝印 | 30–200 元 |
| 高温胶带 | `高温胶带 Kapton` | 固定飞线 | 5–10 元 |
| USB 延长线 | `USB Type-C 延长线` | 拆机状态接线更方便 | 8–20 元 |

**兜底方案接线（仅在用 3.3V 模块时）：**

```
USB-TTL(3.3V) ──► TXS0102(VCCA=1.8从手机附近取? 或模块自带1.8)
注意：更省事的做法仍是直接买带 1.8V 档的 FT232。
```

不建议新手从主板 LDO 偷 1.8V 给电平转换；优先买多电平 FT232。

---

### 3.4 一套「最少下单」购物车（复制用）

| # | 商品（搜索词） | 数量 | 大约 |
|---|---------------|------|------|
| 1 | `FT232RL 1.8V 3.3V 5V USB转TTL` | 1 | ¥20–40 |
| 2 | `杜邦线 母对母` | 1 排 | ¥5 |
| 3 | `漆包线 0.1mm` 或 `30AWG 电子线 彩色` | 1 | ¥8 |
| 4 | `恒温电烙铁`（若已有可跳过） | 1 | ¥50 |
| 5 | `焊锡丝 0.5mm` + `助焊膏` | 各 1 | ¥15 |
| 6 | `数字万用表`（若已有可跳过） | 1 | ¥30 |
| 7 | `手机拆机工具套装` | 1 | ¥15 |

**合计大约：¥80–160**（已有烙铁/万用表可压到 ¥40 以内）。

---

### 3.5 下单检查清单（付款前勾选）

- [ ] 模块标题或详情明确写 **1.8V**
- [ ] 芯片为 FT232 系列（或明确支持 1.8V IO 的同类桥片）
- [ ] 有跳线帽/拨码；说明书写清如何选 1.8V
- [ ] 附带或另买杜邦线
- [ ] **没有**只买 CH340「通用 3.3V/5V」凑合

到货后第一件事：插电脑，把跳线/拨码拨到 **1.8V**，用万用表测模块 `VCC` 排针是否约 1.8V（有的板 VCC 随电平档变化）。确认后再碰手机。

---

## 4. 电脑侧软件准备

### 4.1 Linux（推荐，与本项目环境一致）

```bash
# 安装终端工具
sudo apt update
sudo apt install -y screen picocom minicom

# 插入模块后查看设备节点
ls -l /dev/ttyUSB* /dev/ttyACM*

# 当前用户加入 dialout（一次即可，重新登录生效）
sudo usermod -aG dialout "$USER"

# 打开串口（二选一）
sudo screen /dev/ttyUSB0 115200
# 或
sudo picocom -b 115200 /dev/ttyUSB0
```

退出：

- `screen`：`Ctrl+A` 然后 `K`，再确认 `Y`
- `picocom`：`Ctrl+A` 然后 `Ctrl+X`

### 4.2 Windows（可选）

1. 安装 FTDI VCP 驱动（买 FT232 时店铺一般给链接）  
2. 设备管理器看 COM 口号  
3. 用 [PuTTY](https://www.putty.org/)：Connection type = Serial，Speed = 115200

### 4.3 模块自检（不接手机）

1. 拔掉模块上 TX↔RX 出厂短接帽（若有）  
2. 用杜邦线短接模块自己的 **TXD–RXD**  
3. 打开串口，输入字符应原样回显 → 模块正常  
4. 再改接到手机

---

## 5. 实操步骤（详细）

### 步骤 1：关机拆机

1. 关机，拔掉充电线。  
2. 加热后盖边缘，塑料撬片缓慢分离（玻璃易碎，慢撬）。  
3. **先断开电池排线**（见 `ginkgo-battery-connector.jpg` 一类拆机图），再碰主板。

### 步骤 2：定位测试点

1. 打开 LLDM516 boardview / PCB 图，搜索 `TP0003`、`TP0012`。  
2. 对照主板丝印；附近找可靠 GND（螺丝孔金属环通常可用）。  
3. 万用表确认：未上电时 TP 对地非短路；上电后 TX≈1.8V。

> 若暂时没有 boardview：可先只焊 **疑似 DBG 测试点 + GND**，上电听串口；无输出再换点。**切勿**把 3.3V TX 乱戳一通。

### 步骤 3：焊接飞线

1. 烙铁温度约 300–330℃，蘸助焊剂。  
2. TP 上挂极少量锡 → 漆包线去漆镀锡 → 贴焊。  
3. 另一端焊到杜邦针或直接绞到杜邦线。  
4. 高温胶带固定，避免拉扯扯掉焊盘。  
5. 参考同族操作图：`lavender-smallfull.jpg` / `lavender-smallpins.jpg`。

### 步骤 4：连接 USB-TTL

1. 模块电平拨到 **1.8V**。  
2. 按第 2.2 节交叉连接 TX/RX，接好 GND。  
3. **VCC 不接手机**。  
4. USB 插入电脑，打开 `115200` 终端。

### 步骤 5：上电观察

1. 接回电池，长按开机。  
2. 正常应先看到 **bootloader / XBL/ABL** 类日志。  
3. 刷入带 `console=` / `earlycon=` 的主线后，应出现内核日志直至 login。  
4. 下游 Android 内核可能几乎不吐 UART（lavender 上有同样现象）；**本项目以主线为准**。

### 步骤 6：排错

| 现象 | 可能原因 | 处理 |
|------|----------|------|
| 完全无输出 | TX/RX 接反 | 对调两根信号线试一次 |
| 完全无输出 | 电平不是 1.8V | 查跳线；万用表测模块与手机 TX |
| 乱码 | 波特率不对 | 固定 115200 |
| 乱码 / 间歇 | 接触不良、地线虚焊 | 重焊 GND |
| RX 灯闪但无字 | 电平太低被 3.3V 模块忽略 | 换 1.8V 模块（lavender 踩过的坑） |
| 焊后手机异常 | 焊盘短路 / 电平过高 | 立刻断电，查短路，确认未接 3.3V |

---

## 6. 与本项目 cmdline 的配合

刷机 / boot.img 中保证包含：

```
console=ttyMSM0,115200n8
earlycon=msm_serial_dm,0x4a90000
```

主线 DTS 中 `&uart4` 应为 `okay`（2026-01 起 ginkgo 已合入相关 patch）。

验收：

```text
串口出现 Linux 版本横幅 / earlycon 输出
能得到 shell 或至少看到 panic 原文
```

---

## 7. 本地图片索引

| 文件 | 用途 |
|------|------|
| `ginkgo-usb-ttl-wiring.png` | 本方案接线示意 |
| `ginkgo-edl-testpoints.jpg` | 主板近景（EDL，非 UART） |
| `willow-edl-testpoint.jpg` | Note 8T ISP/EDL 标注 |
| `ginkgo-battery-connector.jpg` | 拆机 / 电池排线 |
| `lavender-smallfull.jpg` 等 | Note 7 焊串口 + FT232 实操参考 |

---

## 8. 参考链接

- 原理图：[Scribd Redmi Note 8 Schematic](https://www.scribd.com/document/461718408/Schematic-Redmi-Note-8-pdf)  
- 原理图下载页：[Elektrotanya LLDM516](https://elektrotanya.com/xiaomi_phone_redmi_note_8_lldm516_schematics.pdf/download.html)  
- 主线 UART patch：[Enable debug UART on ginkgo](https://www.spinics.net/lists/kernel/msg6011109.html)  
- 同族经验：[UART on Redmi Note 7](https://wantguns.dev/blog/uart-on-lavender/)  
- FT232 多电平说明示例：[丢石头 FT232 Multi-Level](https://wiki.diustou.com/cn/FT232_Multi-Level_USB_TTL)

---

## 9. 待办（实机）

- [ ] 取得 LLDM516 boardview，截图标注 TP0003 / TP0012  
- [ ] 万用表确认本机 TX 空闲电平 ≈ 1.8V  
- [ ] 焊线后归档一段 boot serial log 到 `backup/ginkgo/logs/`
