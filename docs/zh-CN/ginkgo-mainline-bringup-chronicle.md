**语言：** [English](../ginkgo-mainline-bringup-chronicle.md) | 简体中文

# Redmi Note 8 (ginkgo) 主线移植工作全记录

> 设备：Xiaomi Redmi Note 8 · codename **ginkgo** · SoC **SM6125** · 序列号 `<serial>`  
> 文档目的：独立记录本仓库为 ginkgo 适配主线 Linux 所完成的全部工作、踩坑与当前状态。  
> 最后更新：2026-08-19（**DRM 已出图 + 内核 fbcon + SPI 触控 + WCN3990 WiFi + Ubuntu GNOME 桌面可见（Adreno 610）+ CPUFreq/Resources + Docker CE**；显示 [出图全记录](./ginkgo-display-complete-2026-08-17.md)，启动控制台 [fbcon 全记录](./ginkgo-fbcon-boot-2026-08-18.md)，触控 [出点全记录](./ginkgo-touch-complete-2026-08-17.md)，WiFi [关联全记录](./ginkgo-wifi-complete-2026-08-18.md)，桌面软件 [Ubuntu 桌面记录](./ginkgo-ubuntu-desktop-2026-08-19.md)，GPU [Adreno 610 桌面](./ginkgo-gpu-desktop-2026-08-19.md)，卡顿与 Resources [桌面性能](./ginkgo-desktop-perf-resources-2026-08-19.md)，容器 [Docker 适配与清华源安装](./ginkgo-docker-2026-08-19.md)）

---

## 1. 项目目标

在 **Redmi Note 8 (ginkgo)** 上运行 **主线 Linux 内核** + 自建 rootfs，实现：

| 阶段 | 目标 | 状态 |
|------|------|------|
| P0 | 串口启动日志 | 部分（`ttyMSM0` 仍 deferred） |
| P1 | eMMC 挂载 rootfs、systemd 启动 | **已打通** |
| P2 | USB RNDIS + SSH 远程调试 | **已打通** |
| P3 | 屏幕显示（DRM/DSI + 背光） | **已打通**（2026-08-17 品红测试图用户可见；2026-08-18 内核 fbcon 启动日志上屏） |
| P4 | 触控（SPI Novatek NT36672A） | **已打通**（2026-08-17 点屏出点） |
| P5 | WiFi（WCN3990 / ath10k_snoc） | **已打通**（2026-08-18 关联 5 GHz VHT80，速率 292.5 Mbps） |
| P6 | Ubuntu 完整桌面（GNOME / gdm） | **已打通**（2026-08-19 Adreno 610；OSM CPUFreq 后整体好转；Resources 温度/GPU 图见 [桌面性能](./ginkgo-desktop-perf-resources-2026-08-19.md)） |
| P7 | Docker（内核 + 引擎） | **已打通**（2026-08-19：`check-config.sh` 退出码 0；清华源 Docker CE 29.7.2。见 [Docker 全记录](./ginkgo-docker-2026-08-19.md)） |

**当前用户可见状态：** 系统可启动，SSH 可连（USB + **WiFi**）；主线 DRM 品红 framebuffer 可见；**内核启动约 7s 后 fbcon 刷日志**；SPI 触控可点；**wlan0 可关联 2.4G/5G**；**Ubuntu 26.04 GNOME 桌面可见（Adreno 610）**，OSM CPUFreq 后整体好转，Resources 温度/GPU 图见 [桌面性能](./ginkgo-desktop-perf-resources-2026-08-19.md)；**Docker Engine 29.7.2 可用**（overlayfs + cgroup v2）。显示：[ginkgo-display-complete-2026-08-17.md](./ginkgo-display-complete-2026-08-17.md)。启动控制台：[ginkgo-fbcon-boot-2026-08-18.md](./ginkgo-fbcon-boot-2026-08-18.md)。触控：[ginkgo-touch-complete-2026-08-17.md](./ginkgo-touch-complete-2026-08-17.md)。WiFi：[ginkgo-wifi-complete-2026-08-18.md](./ginkgo-wifi-complete-2026-08-18.md)。桌面软件：[ginkgo-ubuntu-desktop-2026-08-19.md](./ginkgo-ubuntu-desktop-2026-08-19.md)。GPU：[ginkgo-gpu-desktop-2026-08-19.md](./ginkgo-gpu-desktop-2026-08-19.md)。Docker：[ginkgo-docker-2026-08-19.md](./ginkgo-docker-2026-08-19.md)。早期 `dsi_err status=5` 分析仍保留作历史：[ginkgo-dsi-err-status5-analysis.md](./ginkgo-dsi-err-status5-analysis.md)。

---

## 2. 硬件与软件基线

### 2.1 关键硬件

| 组件 | 型号/说明 |
|------|-----------|
| SoC | Qualcomm SM6125 (Kryo 260) |
| 存储 | eMMC，`root=/dev/disk/by-partlabel/userdata` |
| 显示面板 | Tianma NT36672A，1080×2340，4-lane DSI |
| 面板驱动 IC compatible | `tianma,ginkgo-fhd-video`（主线 `panel-novatek-nt36672a.c`） |
| 面板偏压 | PMI632 **LCDB**（LDO 正偏压 + NCP 负偏压），非 PMI8998 LAB/IBB |
| 背光（下游） | PM6125 PWM + PMI632 GPIO6 使能 + DCS 亮度 |
| 背光（当前主线） | **KTD3136** @ I2C `0x36` + PMI632 GPIO6 HWEN（不再只用 gpio-backlight） |
| 触控 | Novatek **NT36672A SPI** @ QUP SE2（`&spi2`），IRQ GPIO88，RESET GPIO87 |
| 复位 GPIO（面板） | TLMM GPIO90，低有效 |
| USB 调试 | Configfs RNDIS gadget，`192.168.7.2` |

### 2.2 软件栈

- 内核：主线 tree + `config/ginkgo.fragment`
- 板级 DTS：`linux/arch/arm64/boot/dts/qcom/sm6125-xiaomi-ginkgo.dts`
- SoC DTS：`linux/arch/arm64/boot/dts/qcom/sm6125.dtsi`（含多处 ginkgo 专用修复）
- PMIC：`pm6125.dtsi`、`pmi632.dtsi`
- 打包：`scripts/build-kernel.sh` → `scripts/build-bootimg.sh` → `out/boot.img`
- 刷机：TWRP recovery + `FLASH_ROOTFS=0 ./scripts/flash-linux-boot.sh`
- root 密码：`$GINKGO_ROOT_PASSWORD`（overlay 设置）

---

## 3. 已完成的里程碑

### 3.1 eMMC / 系统启动

**现象（早期）：** 启动卡在 CQHCI / eMMC 初始化。

**根因与修复：**

| 问题 | 根因 | 修复 |
|------|------|------|
| eMMC 不 probe | 缺 `CONFIG_ARM_SMMU` | `ginkgo.fragment` 启用 SMMU |
| SDHCI 竞态 | 异步 probe + clock 依赖 | `sdhci-msm.c` 去掉 `PROBE_PREFER_ASYNCHRONOUS`；cmdline 加 `clk_ignore_unused` |
| 错误 SDHCI 控制器 | `sdhc_2` 与实机不符 | ginkgo DTS 中 `&sdhc_2 { status = "disabled"; }` |
| interconnect 缺失 | SM6115 ICC 节点未定义 | `sm6125.dtsi` 添加 `bimc` / `system_noc` / `config_noc` |
| sync_state 死锁 | devlink 与 uart 互等 | cmdline：`fw_devlink.sync_state=disabled` |

**结果：** rootfs 正常挂载，systemd 可进入 multi-user。

### 3.2 USB RNDIS + SSH

**实现：**

- 内核：`CONFIG_USB_CONFIGFS_RNDIS` 等
- rootfs：`usb-gadget-rndis.service`（Configfs gadget）
- 手机 IP：`192.168.7.2`，主机建议 `192.168.7.1`

**PC 端连网注意（重要）：**

不要把 USB 网卡设为默认路由，否则会断主网络。推荐：

```bash
# NetworkManager：永不作默认网关
nmcli connection modify ginkgo-usb-tmp \
  ipv4.method manual ipv4.addresses 192.168.7.1/24 \
  ipv4.never-default yes ipv4.route-metric 5000

nmcli connection up ginkgo-usb-tmp

# SSH 绑定源地址
ssh -b 192.168.7.1 root@192.168.7.2
```

或使用 `scripts/host-usb-connect.sh`（已改为只添加 `192.168.7.0/24` 路由，不改默认网关）。

**结果：** `ssh root@192.168.7.2` 可稳定 ping/登录（USB 枚举后约 8–40s）。

### 3.3 串口

- 硬件：USB-TTL 接 UART（`ttyMSM0` / `uart4` @ `0x4a90000`）
- 监听：`sudo python3 scripts/uart-monitor.py` → 日志在 `backup/ginkgo/logs/uart-*.log`
- **遗留：** `4a90000.serial` 仍 `deferred probe pending`（interconnect + gcc sync_state），不影响 SSH，但串口 console 不完整

### 3.4 触控（SPI Novatek NT36672A）

**2026-08-17 已出点。** 全文：[ginkgo-touch-complete-2026-08-17.md](./ginkgo-touch-complete-2026-08-17.md)

- 总线：QUP SE2 `&spi2`，gpio6–9 `qup02`，IRQ 88，RESET 87，8 MHz，`SPI_MODE_0`
- 驱动：下游 `nt36xxx_spi_c3j` 移植到 `linux/drivers/input/touchscreen/nt36xxx/`
- 固件：内置 `novatek_ts_tianma_fw.bin`，host-download，PID `591F`
- 决定性修复：CS 低有效（不要信 DT `spi-cs-high`）；头文件统一 `#undef CONFIG_FB`；`NVT_TRANSFER_LEN=32`
- 验收：`NVTCapacitiveTouchScreen`，用户确认点屏出点

---

## 4. 显示子系统适配全记录

从 simple-framebuffer 黑屏占位，逐步迁移到完整 DRM/MDSS/DSI 栈。以下按**时间顺序**记录每次改动、现象与结论。

### 4.1 第一阶段：启用 DRM 栈与面板节点

**改动：**

- `config/ginkgo.fragment`：启用 `CONFIG_DRM_MSM*`、`CONFIG_DRM_PANEL_NOVATEK_NT36672A`、`CONFIG_DRM_MIPI_DSI`、fbcon 等
- `sm6125-xiaomi-ginkgo.dts`：删除 `chosen` 里 `simple-framebuffer`；启用 `&mdss`、`&mdss_dsi0`、`&mdss_dsi0_phy`
- 面板：`compatible = "tianma,ginkgo-fhd-video"`，1080×2340 init 序列已在 `panel-novatek-nt36672a.c`
- 供电占位：`panel_vddpos` / `panel_vddneg`（`regulator-fixed` always-on）
- 背光：`gpio-leds` → 后改为 `gpio-backlight`（PMI632 GPIO6）

**现象：** 屏幕仍黑；`/dev/dri/` 不存在；面板无驱动绑定。

---

### 4.2 第二阶段：面板驱动未加载（`-m` 模块陷阱）

**现象：** MIPI 设备 `5e94000.dsi.0` 存在，但无 driver；`modprobe panel-novatek-nt36672a` 失败（rootfs 无模块）。

**根因：**

1. `CONFIG_BACKLIGHT_CLASS_DEVICE=m`（defconfig）导致 Kconfig 强制 `CONFIG_DRM_PANEL_NOVATEK_NT36672A=m`
2. `build-kernel.sh` 仅在首次生成 `.config` 时 merge fragment，后续编译可能丢失 `=y`

**修复：**

```kconfig
CONFIG_BACKLIGHT_CLASS_DEVICE=y
CONFIG_BACKLIGHT_GPIO=y
CONFIG_DRM_PANEL_NOVATEK_NT36672A=y
```

`build-kernel.sh` 改为**每次编译都 merge** `ginkgo.fragment`。

**结果：** 面板驱动编入 vmlinux，但仍无 `/dev/dri`（组件未完整 bind）。

---

### 4.3 第三阶段：DSI PHY 地址解析错误

**现象：** `msm_dsi_phy 0.phy: Couldn't identify PHY index`

**根因：** `sm6125.dtsi` 中 MDSS `#address-cells/#size-cells` 曾为 `<1>`，子节点 `phy@5e94400` 被解析为地址 `0`。

**修复：**

- MDSS `#address-cells = <2>;` `#size-cells = <2>;`
- `mdss_dsi0_phy` compatible 改为 `qcom,dsi-phy-14nm-2290`

**结果：** PHY index 错误消失，`msm_dsi_phy` 正常 probe。

---

### 4.4 第四阶段：pinctrl 与 reset GPIO

**现象 A：** `deferred: wait for supplier mdss-te-active-state`（面板永不 probe）

**修复：** 从面板节点移除 `mdss_te_active` pinctrl（TE 引脚 devlink 死锁）；保留 `mdss_dsi_active`（GPIO90 复位脚配置）。

**现象 B：** `failed to get reset gpio from DT`

**根因：** `CONFIG_PINCTRL_MSM` 未启用，TLMM (`500000.pinctrl`) 无驱动，GPIO90 不可用。

**修复：**

```kconfig
CONFIG_PINCTRL_MSM=y
CONFIG_PINCTRL_SM6125=y
```

**结果：** `sm6125-tlmm` 驱动加载；`panel-tianma-nt36672a` 可绑定；`msm_drm` 可初始化。

---

### 4.5 第五阶段：LCDB 面板偏压（当前最新）

**现象：** `msm_drm` 已初始化，`/dev/dri/card0` 和 `/dev/fb0` 存在，但：

```
panel-tianma-nt36672a: failed to send DCS Init 1st Code: -22
disp_cc_mdss_pclk0_clk_src: rcg didn't update its configuration
[drm] vblank wait timed out on crtc 0
```

屏幕依旧全黑；`gpio-backlight` 报告 `brightness=1`（仅开关，无 PWM 调光）。

**根因（偏压）：** 占位 `regulator-fixed` 无法驱动 PMI632 LCDB 硬件；主线原先**无** LCDB 驱动。

**本次改动：**

1. **新增驱动** `linux/drivers/regulator/qcom-qpnp-lcdb-regulator.c`  
   - 从下游移植，去掉 `qpnp-revid` 依赖  
   - `CONFIG_REGULATOR_QPNP_LCDB=y`

2. **`pmi632.dtsi`** 增加 `qpnp-lcdb@ec00` 节点：
   - `lcdb_ldo_vreg`（vddpos，5.4V）
   - `lcdb_ncp_vreg`（vddneg，5.4V）
   - `lcdb_bst_vreg`（boost）

3. **`sm6125-xiaomi-ginkgo.dts`：**
   - 删除 `panel_vddpos` / `panel_vddneg` 占位
   - `vddpos-supply = <&lcdb_ldo_vreg>`
   - `vddneg-supply = <&lcdb_ncp_vreg>`
   - `refgen-supply = <&refgen_fixed>`（下游无 MMIO refgen 节点，用 fixed 占位）

**刷机后实测（`uart-20260808-031320.log` / SSH）：**

```
LCDB: LCDB module successfully registered! lcdb_en=1 ldo_voltage=5500mV ncp_voltage=6000mV
[drm] Initialized msm 1.13.0
panel-tianma-nt36672a: failed to send DCS Init 1st Code: -22
```

| 检查项 | 结果 |
|--------|------|
| LCDB probe | 成功 |
| lcdb_ldo | enabled, 5.5V |
| lcdb_ncp | enabled |
| panel 驱动绑定 | 成功 |
| DCS 初始化 | **失败 -22** |
| 背光 | GPIO 使能 ON，无 PWM，用户仍觉全黑 |
| 屏幕 | **完全黑屏** |

**结论：** 电源链路（LCDB）已打通，当前瓶颈转为 **DSI 时钟（disp_cc RCG）+ DCS 命令链路**。

---

### 4.6 第六阶段：DCS / HS / DPMS / 背光（2026-08 上旬）

随后几天打通了 DCS 顺序、14nm `LANE_CTRL` bit24、PHY LDO `0x1c`、clamp、`byte_intf` 时钟、DPMS unblank、KTD3136 背光。到这一步：`panel init complete`、`power mode 0x9c`、INTF 60fps、背光可开，用户仍可能是「有背光、画面全黑」。

详见 [ginkgo-display-bringup-methodology.md](./ginkgo-display-bringup-methodology.md)。

---

### 4.7 第七阶段：DSI TPG 证明面板通，FIFO `0xcccc` 卡住 DPU 像素

决定性实验：打开 DSI 内部 TPG（旁路 DPU）后用户能看到棋盘格/整屏绿，`LANE=0x1f00` `FIFO=0x1010`。关掉 TPG、改走 DPU 像素则 FIFO 粘在 `0xcccc1019`（VIDEO_MDP 同时 overflow+underflow）。

结论：**面板 / PHY / 整宽 1080 都没问题**；问题在 INTF→DSI MDP 输入。

---

### 4.8 第八阶段：出图（2026-08-17）

真正根因与修复：

1. **INTF_1 programmable fetch**：catalog 默认 24 行，面板 VFP 只有 10。预取窗口落在 DSI BLLP 上，第一帧 VFP 后 FIFO 打成 `0xcccc`。INTF_1 `prog_fetch_lines_worst_case = 0`。
2. **kickoff 顺序**：先 `host_enable`（INTF 关着做 20ms HS cycle），再 `enable_timing(1)`。INTF 喷着时 reset DSI 会把 FIFO 永久打爆。
3. **MDP 时钟**：用 `mode->clock * 1000`；`clk_inefficiency_factor=218`（220 会 `MODE_CLOCK_HIGH` 丢掉全部 mode）。
4. **HS cycle**：20ms SOFT_RESET 后必须恢复原 `CLK_CTRL`，不能留下 `DYNAMIC_FORCE_ON`。

验收：`LANE=0x1f00`、`FIFO=0x1010`、INTF 60fps、无运行期 `dsi_err`、品红 fb 用户确认「有点粉色」。

完整经历与心得：[ginkgo-display-complete-2026-08-17.md](./ginkgo-display-complete-2026-08-17.md)。

---

## 5. 显示相关文件清单

### 5.1 内核配置

| 文件 | 作用 |
|------|------|
| `config/ginkgo.fragment` | 所有 ginkgo 内核选项碎片 |

显示相关关键项：

- `CONFIG_DRM_MSM*`、`CONFIG_DRM_PANEL_NOVATEK_NT36672A=y`
- `CONFIG_BACKLIGHT_CLASS_DEVICE=y`、`CONFIG_BACKLIGHT_GPIO=y`
- `CONFIG_PINCTRL_MSM=y`、`CONFIG_PINCTRL_SM6125=y`
- `CONFIG_REGULATOR_QPNP_LCDB=y`
- `CONFIG_SM_DISPCC_6125=y`、`CONFIG_INTERCONNECT_QCOM_SM6115=y`

### 5.2 设备树

| 文件 | 显示相关改动 |
|------|----------------|
| `sm6125-xiaomi-ginkgo.dts` | 面板、背光、MDSS/DSI 使能、LCDB 接线、refgen |
| `sm6125.dtsi` | MDSS 地址单元、DSI PHY compatible、interconnect |
| `pmi632.dtsi` | LCDB 节点（本次新增） |

### 5.3 驱动源码

| 文件 | 说明 |
|------|------|
| `linux/drivers/gpu/drm/panel/panel-novatek-nt36672a.c` | ginkgo 1080×2340 init 序列 |
| `linux/drivers/regulator/qcom-qpnp-lcdb-regulator.c` | **本次新增**，PMI632 LCDB |
| `linux/drivers/regulator/Kconfig` | `REGULATOR_QPNP_LCDB` |
| `linux/drivers/regulator/Makefile` | 编译 LCDB 驱动 |

### 5.4 脚本

| 脚本 | 说明 |
|------|------|
| `scripts/build-kernel.sh` | 每次 build 都 merge fragment |
| `scripts/build-bootimg.sh` | 打包 boot.img |
| `scripts/flash-linux-boot.sh` | recovery adb 刷 boot |
| `scripts/host-usb-connect.sh` | USB 连手机，不抢默认路由 |
| `scripts/uart-monitor.py` | 串口日志采集 |

---

## 6. 构建与刷机流程

```bash
cd .

# 编译内核 + DTB
./scripts/build-kernel.sh

# 打包 boot.img
./scripts/build-bootimg.sh

# 手机进入 TWRP recovery，USB 连接后：
FLASH_ROOTFS=0 ./scripts/flash-linux-boot.sh
```

仅更新内核时 `FLASH_ROOTFS=0`；需要 overlay 变更时去掉该变量或设 `FLASH_ROOTFS=1`。

---

## 7. 调试命令速查

### 7.1 SSH（不抢默认路由）

```bash
nmcli connection up ginkgo-usb-tmp   # 需预先配置 never-default
ssh -b 192.168.7.1 root@192.168.7.2
```

### 7.2 显示链路

```bash
# 驱动与设备节点
ls -la /dev/dri/card0 /dev/fb0
readlink /sys/bus/mipi-dsi/devices/5e94000.dsi.0/driver
readlink /sys/bus/platform/devices/500000.pinctrl/driver

# _regulator_
grep -l lcdb /sys/class/regulator/regulator.*/name | while read f; do
  d=$(dirname "$f"); echo "$(cat $f): $(cat $d/state)"
done

# 内核日志
dmesg | grep -iE 'LCDB|panel|DCS|msm_drm|disp_cc|dsi|vblank|deferred'
```

### 7.3 串口

```bash
sudo python3 scripts/uart-monitor.py
# 日志：backup/ginkgo/logs/uart-YYYYMMDD-HHMMSS.log
```

---

## 8. 错误现象 → 根因速查表

| 日志/现象 | 根因 | 修复状态 |
|-----------|------|----------|
| eMMC / CQHCI 卡死 | clk 竞态、缺 SMMU | 已修复 |
| USB extcon deferred | 缺 interconnect | 已修复（去 extcon + ICC） |
| `msm_dsi_phy 0.phy` | MDSS `#address-cells=1` | 已修复 |
| 无 `/dev/dri`，面板无驱动 | 面板驱动 `=m` 未加载 | 已修复 |
| `wait for supplier mdss-te-active-state` | TE pinctrl devlink | 已修复（去掉 TE pinctrl） |
| `failed to get reset gpio` | 缺 `PINCTRL_MSM` | 已修复 |
| `failed to send DCS Init -22` | DSI 时钟/链路未就绪 | **已修复**（host 顺序 + 时钟） |
| `disp_cc pclk0 rcg didn't update` | disp_cc 时钟配置 | **已修复**（出图时 pclk=183 MHz） |
| vblank timeout | 面板未出图 | **已修复**（INTF 60fps） |
| 有背光画面全黑 | INTF PROG_FETCH × DSI BLLP → FIFO `0xcccc` | **已修复**（INTF_1 预取=0 + kickoff 顺序） |
| 完全没背光 | KTD3136 未驱动 | **已修复**（`ktd3136.c` @ I2C 0x36） |
| `ttyMSM0` deferred | uart interconnect | 未修复（低优先级） |
| SSH 时好时坏 | USB 枚举时序 / NM 配置 | 部分缓解（每次 reboot 跑 `ginkgo-usb-ssh`） |

---

## 9. 下游 vs 主线显示供电对照

| 下游 (`dsi_panel_pwr_supply`) | 主线 (`panel-novatek-nt36672a`) | 当前接线 |
|------------------------------|----------------------------------|----------|
| `vddio` → L9A 1.8V | `vddio-supply` | `vreg_l9a` ✓ |
| `lab` → `lcdb_ldo` | `vddpos-supply` | `lcdb_ldo_vreg` ✓ |
| `ibb` → `lcdb_ncp` | `vddneg-supply` | `lcdb_ncp_vreg` ✓ |
| `vdda` 1.2V | DSI/PHY `vdda-supply` | `vreg_l18a` ✓ |
| `refgen`（内部 supply） | `refgen-supply` | `refgen_fixed` 占位 |
| PWM + GPIO 背光 | `backlight` | **KTD3136**（I2C）+ GPIO6 HWEN ✓ |
| DCS 亮度控制 | 面板驱动 on_cmds | 链路通后可用；亮度现以 KTD3136 为准 |

---

## 10. 下一步建议（显示 + fbcon + 触控 + WiFi + GNOME 已通）

显示 P3、内核 fbcon、触控 P4、WiFi P5、Ubuntu GNOME（Adreno 610）已完成。桌面可见但会稍微有点卡，见 [ginkgo-gpu-desktop-2026-08-19.md](./ginkgo-gpu-desktop-2026-08-19.md)。剩余按优先级：

### 10.0 桌面流畅度（非阻塞）

- idle 后确认 gnome-shell CPU 是否随 shader cache 下降
- GPU OPP / interconnect；必要时更轻的会话（仍走原生 GPU，不要退回 swrast）

### 10.1 其它外设

- 蓝牙（同颗 WCN3990，依赖已起来的 MPSS / 固件，尚未单独验收）
- 音频、相机等

### 10.2 显示非阻塞遗留

- encoder vsync IRQ ~11 Hz vs INTF 硬件 60fps（不影响可见图像）
- 下游 PWM 背光未复刻（当前 KTD3136 够用）
- GPIO89 TE：需在避免 devlink 死锁的前提下再评估
- 把 `fastboot boot` 固化进 boot 分区（**需用户明确要求**）

### 10.3 真实 refgen（低优先级）

- 下游无独立 DT 节点；当前 fixed 占位可保留

---

## 11. 日志归档

| 日志 | 内容摘要 |
|------|----------|
| `uart-20260808-001517.log` | 早期 eMMC/启动 |
| `uart-20260808-023855.log` | DSI PHY 修复后，无面板驱动 |
| `uart-20260808-024802.log` | reset GPIO 失败（无 PINCTRL_MSM） |
| `uart-20260808-025327.log` | PINCTRL 修复后 msm_drm 初始化，DCS -22 |
| `uart-20260808-031320.log` | **LCDB 成功后**，DCS -22 + disp_cc RCG 警告 |
| `uart-20260808-225704.log` | build #45：panel init OK，`dsi_err status=5`，IOMMU @ 0x5c003000 |

---

## 12. 版本时间线

| 时间（约） | 内核变化 | 显示进展 |
|------------|----------|----------|
| 2026-08-08 00:15 | DRM 栈首次启用 | 无 dri |
| 02:38 | PHY address-cells 修复 | DSI PHY OK |
| 02:43 | 面板 built-in（BACKLIGHT_CLASS） | 仍 deferred |
| 02:49 | 去掉 TE pinctrl | reset GPIO 仍失败 |
| 02:52 | PINCTRL_MSM 启用 | msm_drm 初始化，DCS -22 |
| 03:12 | LCDB 驱动 + 真实偏压 | LCDB OK，DCS 仍 -22，屏全黑 |
| 22:51 | build #44 bridge 顺序 | DCS OK，`dsi_err status=5` |
| 22:57 | build #45 PHY timing + 0.9V | 仍 `status=5`，闪一下后黑屏有背光 |
| 2026-08 上旬–中旬 | HS / clamp / LDO / DPMS / KTD3136 | 有背光；DSI TPG 可见棋盘格/绿 |
| **2026-08-17** | 关 INTF_1 预取 + 先 DSI 后 INTF + MDP 400 MHz | **品红上屏，P3 完成** |
| **2026-08-17 晚** | NT36672A SPI：MODE_0 + CONFIG_FB 结构体对齐 + 32B FIFO 下载 | **触控出点，P4 完成** |
| **2026-08-18** | WCN3990：1MB MSA、空 CAL 协议、延迟 BSS-peer vdev-start、数据 MCS 缓存 | **WiFi 关联 5 GHz VHT80，P5 完成** |
| **2026-08-18 晚** | boot.img / DTS cmdline 加 `console=tty0`（fbcon） | **内核启动日志上屏** |
| **2026-08-19 凌晨** | TUNA 源 + `ubuntu-desktop` + 补配 gdm3 | **GNOME 会话已起；屏幕仍黑（无 GPU）** |

---

## 13. 总结

本项目已将 **Redmi Note 8** 推进到「**主线 Linux 可启动、USB/WiFi SSH、DRM 真显示已出图、内核 fbcon 启动日志上屏、SPI 触控可点、WiFi 可关联、Ubuntu 完整桌面软件已装**」的阶段。GNOME 会话在跑，**可见桌面尚未出图**（无 Adreno，Mesa 误用 `msm_dri`）。

- **软件栈：** DRM/MDSS/DPU/DSI/PHY/面板/LCDB/KTD3136 均已工作；fbcon + `console=tty0` 让内核日志上屏；NT36672A SPI 触控 + Tianma 固件 host-download 已工作；WCN3990 `ath10k_snoc` + 本机 `WLAN.HL.3.0.2` 已工作
- **硬件输出：** `LANE=0x1f00`、`FIFO=0x1010`、INTF 60fps；用户确认品红测试图；启动约 7s 后企鹅 + 内核日志；`NVTCapacitiveTouchScreen` 点屏出点；`<test-ap-5g>` VHT80 关联，速率 292.5 Mbps
- **出图关键：** INTF programmable fetch 与 DSI BLLP 冲突；不要在 INTF 跑着时 20ms reset DSI
- **启动控制台关键：** `CONFIG_FRAMEBUFFER_CONSOLE` 不够，boot.img cmdline 必须有 `console=tty0`；不要回退 simplefb
- **出点关键：** CS 必须 `SPI_MODE_0`；`nt36xxx.h` 统一 `#undef CONFIG_FB`；FIFO-only 时 `NVT_TRANSFER_LEN=32`
- **WiFi 关键：** MSA 1MB 且必须 hyp_assign；空 CAL 先答 `CAL_DOWNLOAD` 再 `CAL_REPORT`；STA 先 BSS peer 再 vdev-start（不要 self-peer）；速率缓存最后一次数据 MCS
- **桌面关键：** TUNA `ubuntu-ports`；`gdm3` 必须配置完（`gdm` 用户 + PAM）；黑屏先查 Mesa/`msm_dri`，不要当 DSI 回归

下一阶段优先让 GNOME **在软件渲染下可见**；Adreno 和蓝牙另开。

完整显示经历：[ginkgo-display-complete-2026-08-17.md](./ginkgo-display-complete-2026-08-17.md)。  
完整启动 fbcon 经历：[ginkgo-fbcon-boot-2026-08-18.md](./ginkgo-fbcon-boot-2026-08-18.md)。  
完整触控经历：[ginkgo-touch-complete-2026-08-17.md](./ginkgo-touch-complete-2026-08-17.md)。  
完整 WiFi 经历：[ginkgo-wifi-complete-2026-08-18.md](./ginkgo-wifi-complete-2026-08-18.md)。  
Ubuntu 桌面经历：[ginkgo-ubuntu-desktop-2026-08-19.md](./ginkgo-ubuntu-desktop-2026-08-19.md)。

---

*本文档为独立全记录。后续里程碑请在本文件末尾追加章节或更新第 4、10、12 节。*
