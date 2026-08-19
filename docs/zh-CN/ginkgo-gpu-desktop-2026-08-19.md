**语言：** [English](../ginkgo-gpu-desktop-2026-08-19.md) | 简体中文

# Redmi Note 8 (ginkgo) 主线 Adreno 610：Ubuntu 桌面从黑屏到可见

> 设备：Xiaomi Redmi Note 8 · codename **ginkgo** · SoC **SM6125 (trinket)** · 序列号 `<serial>`  
> GPU：Qualcomm **Adreno 610** · chipid **`0x06010000`** · 无独立 GMU 固件（`a6xx_gmuwrapper`）  
> 系统：主线 Linux 7.0 + Ubuntu 26.04 LTS（resolute）arm64，rootfs 在 userdata  
> 验收日：2026-08-19 01:53 · **用户确认已进入 GNOME 桌面**；整体会稍微有点卡  
> 前置：显示 P3、fbcon、触控 P4、WiFi P5、Ubuntu 桌面软件（会话已起但黑屏）均已通。

**关联文档 / skill**

| 文档 | 内容 |
|------|------|
| [ginkgo-ubuntu-desktop-2026-08-19.md](./ginkgo-ubuntu-desktop-2026-08-19.md) | 清华源 + 完整 GNOME 装上；当时黑屏、无 GPU |
| [ginkgo-display-complete-2026-08-17.md](./ginkgo-display-complete-2026-08-17.md) | DPU→DSI 出图像素链路（不要回退） |
| [ginkgo-mainline-bringup-chronicle.md](./ginkgo-mainline-bringup-chronicle.md) | 全机 bring-up 时间线 |
| [firmware/ginkgo/README.md](../firmware/ginkgo/README.md) | zap / SQE 来源 |
| [usb-connect.sh](../scripts/usb-connect.sh) | 每次 reboot 后重配 RNDIS |
| [reboot-fastboot.sh](../scripts/reboot-fastboot.sh) | 从 Ubuntu 进 fastboot |

验证仍用 `fastboot boot out/boot.img`，**不要** `fastboot flash boot`。

---

## 0. 一句话结论

桌面从黑屏变成可见，不是因为再装一遍 GNOME 或改 DSI，而是 **主线 `msm` 真正绑上了 Adreno 610**，Mesa 能在同一块 DRM 设备上做 3D 合成。

已经落地的：

1. SM6125 DTS 补上 `gpu@5900000` + `gmu-wrapper` + `gpucc` + `adreno_smmu`（模板是 SM6115，时钟控制器用本 SoC 的 `qcom,sm6125-gpucc`）。  
2. **Zap 用本机签名**（vendor `a610_zap.mdt` + `.b00/.b01/.b02`）；**SQE 用 linux-firmware**（本机 `a630_sqe.fw` 版本 `0x187`，主线要求 `>= 0x190`）。  
3. Xiaomi 把 GPU PIL 保留内存改到 **`0x57515000`**，不是 SoC 默认 `0x57115000`。  
4. `highest_bank_bit` 与 DPU/UBWC_CFG 对齐为 **14**（先 13、再 15 都会让 GPU 画的帧 DPU 读不懂，看起来还是黑屏）。  
5. `gdm` 作为 `display-manager.service` 开机自启。

用户可见：**GNOME 桌面已经出来**。随后的卡顿、Resources 无温度/GPU 图，见 [ginkgo-desktop-perf-resources-2026-08-19.md](./ginkgo-desktop-perf-resources-2026-08-19.md)（OSM CPUFreq + TSENS + `gpu_busy_percent`）。

---

## 1. 黑屏当时是哪一层

桌面软件文档里已经定位过：GNOME 进程层成功，DSI/DPU 也没回归。坏的是 **3D/合成**：

```
/dev/dri/card0  →  msm_dpu（display-controller@5e01000）
Mesa            →  msm_dri.so
日志            →  egl: failed to create dri2 screen
gnome-shell     →  ~一核 100%，看不见第一帧
```

DTS 里没有 `gpu@`，rootfs 里没有 Adreno/zap。`renderD128` 当时也挂在 DPU 上。

主线 `msm` 的模型是：**KMS（DPU）和 GPU（Adreno）绑在同一张 DRM 卡上**，不会额外出现 `card1`。GPU probe 成功之后，还是 `card0`，只是这块卡现在有 3D。

不要把那次黑屏当成 P3 显示回归，也不要用 `kms_swrast` 当最终方案——用户要的是原生 GPU。

---

## 2. 硬件与主线驱动事实

| 项 | 值 |
|----|----|
| GPU | Adreno 610，`qcom,adreno-610.0` |
| chipid | `0x06010000`（下游 `trinket-gpu.dtsi`） |
| 寄存器 | `0x5900000` / `cx_mem@0x599e000` / `cx_dbgc@0x5961000` |
| IRQ | GIC SPI **177** |
| SMMU | `0x59a0000`，`qcom,adreno-smmu` |
| GMU | **wrapper**（`gmu@596a000`），没有独立 `a630_gmu.bin` |
| GPUCC | 内核已有 `gpucc-sm6125.c`，compatible `qcom,sm6125-gpucc`，fragment 打开 `CONFIG_SM_GPUCC_6125=y` |
| 频率 | 下游单 SKU：320 / 465 / 600 / 745 / 820 / 900 / 950 MHz；trinket **没有 speedbin** |
| Zap PAS id | **13**（与 `a6xx_gpu.c` `GPU_PAS_ID` 一致） |

主线 `a6xx_catalog.c` 明确写了 A610 覆盖 SM6125（trinket）、SM6115（bengal）、SM6225（khaje）。Trinket 只有一个 SKU，OPP 不要 `nvmem` / `opp-supported-hw`。

---

## 3. 做了哪些事（按坑）

### 3.1 DTS：把 SM6115 的 GPU 块搬到 SM6125

`sm6125.dtsi` 原先只有 8 KB 的 `gpu_mem@57115000`，没有 gpu/gmu/gpucc/adreno_smmu。

对照 `sm6115.dtsi` 写入：

- `gpu@5900000`：`qcom,adreno-610.0`，时钟 `core/iface/mem_iface/alt_mem_iface/gmu/xo`，`qcom,gmu = <&gmu_wrapper>`，`status = "disabled"`  
- `gmu@596a000`：`qcom,adreno-gmu-wrapper`，CX/GX GDSC  
- `clock-controller@5990000`：`qcom,sm6125-gpucc`（**不是** `sm6115-gpucc`），父时钟只有 XO + `GCC_GPU_GPLL0_CLK_SRC`（yaml 两个，没有 DIV）  
- `iommu@59a0000`：IRQ 列表跟 SM6115 相同（163、167–174）

板级 `sm6125-xiaomi-ginkgo.dts`：`&gpu { status = "okay"; }`，zap `firmware-name`。

OPP 接到已有的 `rpmpd_opp_*`。SM6125 没有 `turbo_plus`，950 MHz 用 `turbo`。

### 3.2 Zap 地址必须用 Xiaomi 的 `0x57515000`

下游 `ginkgo-trinket-memory.dtsi` 在加大 ADSP 之后，把 IPA/GPU PIL 从 `0x571xxxxx` 挪到 `0x575xxxxx`：

```
pil_gpu_mem: 0x57515000 / 8 KB
```

本机 vendor 里的 zap 按这块板签名。主线 SoC dtsi 仍写 `0x57115000`，板级覆盖：

```dts
&gpu_mem {
	reg = <0x0 0x57515000 0x0 0x2000>;
};
```

ELF 只有一个 `PT_LOAD`，`qcom_mdt_get_size` ≈ 4 KB，8 KB 够。

### 3.3 固件：zap 本机、SQE 上游

从仍在的 **vendor 分区**（`mmcblk0p85`）取出：

| 文件 | 用途 |
|------|------|
| `a610_zap.mdt` + `.b00/.b01/.b02` | TZ PAS 加载；路径 `qcom/sm6125/xiaomi/ginkgo/` |
| `a610_zap.elf` | 对照，不安装 |
| `a630_sqe.fw`（vendor，31980 B） | **不要用**；payload 版本 `0x187` |
| `a630_sqe.fw`（linux-firmware，34188 B） | **要用**；版本 `0x207` ≥ `0x190` |

`a6xx_ucode_check_version()` 对 `a630_sqe.fw` 要求 `>= 0x190` 或 patched nibble `0xa`。本机 SQE 过不了，内核会 `-EPERM`，GPU 看起来 probe 了但 3D 起不来。

SQE 不经 TZ 签名，可以换上游。Zap **必须**本机。

投放路径：

- 仓库：`firmware/ginkgo/gpu/`  
- initramfs：`/lib/firmware/qcom/...`（内核还在 initramfs root 时就能 `request_firmware_direct`）  
- overlay：同样路径，落到 userdata  
- `scripts/configure-rootfs.sh` / `scripts/build-initramfs.sh` 负责复制  

### 3.4 `gmu` 没有自己的 platform driver 是正常的

`596a000.gmu` 在 sysfs 里 **unbound**。GMU wrapper 是 Adreno probe 里通过 `qcom,gmu` phandle 初始化的，不是独立驱动。`gpucc ... sync_state() pending due to 596a000.gmu` 可以忽略。

成功标志：

```
msm_dpu ... bound 5900000.gpu (ops a3xx_ops)
loaded qcom/a630_sqe.fw from new location
/sys/kernel/debug/dri/0/gpu → gpu-initialized: 1, revision: 610 (6.1.0.0)
```

固件请求走 DPU 这块 DRM 设备的名字，日志里写 `msm_dpu ... adreno_request_fw`，不是写错设备。

### 3.5 UBWC `highest_bank_bit`：13 / 15 都会让桌面继续黑

| 尝试 | GPU 侧 HBB | UBWC_CFG / DPU | 现象 |
|------|------------|----------------|------|
| 主线 a610 默认硬编码 | 13 | 14 | `Inconclusive ... 13 vs 14`；GPU 在提交，合成帧 DPU 对不上 |
| 只删掉 13、走通用 a6xx | 15 | 14 | `Inconclusive ... 15 vs 14`；同样对不上 |
| 跟 SoC `qcom_ubwc_cfg` | **14** | **14** | 警告消失；用户看见桌面 |

下游 `trinket-gpu.dtsi` 也是 `qcom,highest-bank-bit = <14>`。`sm6125_data.highest_bank_bit = 14`。

改动在 `a6xx_calc_ubwc_config()`：A610 用 `common_cfg->highest_bank_bit`，swizzle 仍是 `0x7`。

### 3.6 gdm 装完不等于会开机自启

`ubuntu-desktop` 装在已经到达 `graphical.target` 之后时，gdm 可能是 `disabled`，没有 `display-manager.service`。这次开机自启靠：

```
/etc/systemd/system/display-manager.service → gdm.service
```

overlay 里同样放了这条 symlink，下次 `fastboot boot` 还会铺上。

---

## 4. 验收（2026-08-19 01:53）

用户确认：**已经进入 Ubuntu 桌面。**

机上对照：

```
gpu-initialized: 1
revision: 610 (6.1.0.0)
dmesg：bound 5900000.gpu；loaded qcom/a630_sqe.fw
dmesg：**没有** Inconclusive highest_bank_bit
gdm.service：enabled + active
gnome-shell --mode=ubuntu
日志：Created gbm renderer for '/dev/dri/card0'
日志：GNOME Shell started
**没有** egl: failed to create dri2 screen
fb0/blank=0，card0-DSI-1 dpms=On
GPU fence 在往前走（提交并 retire）
```

Mesa 26：`libgallium-26.0.3` + `libEGL_mesa`，gnome-shell 映射了 `/dev/dri/card0`。仍然是一张 `card0`（DPU+GPU），这是主线 msm 的正常形态。

### 4.1 「稍微有点卡」

这是当前已知边界，不是「GPU 没工作」：

| 因素 | 说明 |
|------|------|
| GPU | Adreno 610，峰值 950 MHz，手机 SoC |
| 面板 | 1080×2340，像素量大约是 1080p 的 2.1 倍 |
| 合成器 | GNOME + Wayland + `GSK_RENDERER=ngl` |
| CPU | Kryo 260（4×A73 + 4×A53） |
| 内存 | ~5.5 GiB，无 swap；gnome-shell RSS 曾到 ~380 MB |
| 首次进桌面 | Mesa shader cache 要编一轮，CPU 会冲高，之后会好一些 |

fence 能提交并 retire，说明 3D 路径在跑。卡是 **610 推 GNOME 全桌面** 的性能，不是软件渲染那种一核打满还不出帧。

后续如果要再顺一点（另开任务，先不要动显示预取 / 触控 / WiFi）：

- 确认 idle 时 gnome-shell CPU 是否降下来（shader cache 热了之后）  
- 评估更轻的会话（仍要原生 GPU，不是 swrast）  
- GPU OPP / interconnect 带宽（这次没接 `gfx-mem`）  
- 热和频率是否停在低 OPP  

---

## 5. 仓库里对应改动

| 路径 | 作用 |
|------|------|
| `linux/arch/arm64/boot/dts/qcom/sm6125.dtsi` | gpu / gmu-wrapper / gpucc / adreno_smmu + `gpu_mem` |
| `linux/arch/arm64/boot/dts/qcom/sm6125-xiaomi-ginkgo.dts` | enable GPU；zap 路径；PIL `0x57515000` |
| `linux/drivers/gpu/drm/msm/adreno/a6xx_gpu.c` | A610 `highest_bank_bit` 跟 SoC UBWC_CFG |
| `config/ginkgo.fragment` | `CONFIG_SM_GPUCC_6125=y` |
| `firmware/ginkgo/gpu/` | 本机 zap + linux-firmware SQE |
| `scripts/build-initramfs.sh` | 把 GPU 固件打进 initramfs 和 overlay |
| `scripts/configure-rootfs.sh` | 完整 rootfs 同样安装固件 |
| `rootfs-overlay/etc/systemd/system/display-manager.service` | 开机起 gdm |

---

## 6. 不要做

| 不要 | 原因 |
|------|------|
| 为「更顺」去改 DSI 预取 / pinmux / `CONFIG_FB` | 像素链路是好的 |
| 回退 simplefb | 桌面走 DRM/KMS |
| 用 `kms_swrast` / `LIBGL_ALWAYS_SOFTWARE` 当长期方案 | 用户要原生 GPU；那是黑屏时的权宜之计 |
| 换 linux-firmware 的通用 `a610_zap` | 设备签名，PAS 会拒 |
| 用 vendor 分区里那份 `a630_sqe.fw` | 版本太旧，主线直接拒 |
| 把 zap PIL 改回 `0x57115000` | Xiaomi 板级是 `0x57515000` |
| 把 a610 HBB 再写死成 13 或走通用 15 | 和 DPU 的 14 错位，合成画面是黑的 |
| `fastboot flash boot` | 本仓库约定只 `fastboot boot` |
| bounce `wlan0` / 改 MAC | 会搞崩 WLAN.HL.3.x |
| 看到 `596a000.gmu` unbound 就以为 GMU 没起来 | wrapper 不是独立 platform 驱动 |
| 看到只有 `card0` 就以为 GPU 没注册 | msm 是 DPU+GPU 同一张卡 |

第一次 `fastboot boot` 仍可能 Sending 失败：**不要 usbreset**。`killall -9 fastboot` 再试。主机 NetworkManager 会冲掉 `enx*` 的 `192.168.7.1`，SSH 前把该口设成 unmanaged 更稳。

---

## 7. 和其它完成文档的边界

| 问题 | 文档 |
|------|------|
| 有背光无图像、FIFO、INTF 预取 | [显示出图](./ginkgo-display-complete-2026-08-17.md) |
| 清华源、装完整 GNOME、gdm 坑、**当时黑屏** | [Ubuntu 桌面软件](./ginkgo-ubuntu-desktop-2026-08-19.md) |
| Adreno 610、桌面可见 | **本文** |
| 卡顿、Resources 温度/GPU 图、BWMON 卡死 | [桌面性能](./ginkgo-desktop-perf-resources-2026-08-19.md) |
| 扫网 / 关联 | [WiFi](./ginkgo-wifi-complete-2026-08-18.md) |

---

## 8. 回归命令

```bash
# 内核里 GPU 是否起来
dmesg | grep -iE 'bound 5900000.gpu|a630_sqe|Inconclusive highest_bank'
head -12 /sys/kernel/debug/dri/0/gpu

# 桌面
systemctl is-active gdm.service
pgrep -a gnome-shell
journalctl -b | grep -E 'dri2 screen|GNOME Shell started|Created gbm renderer'

# 显示没被合成器关掉
cat /sys/class/graphics/fb0/blank
cat /sys/class/drm/card0-DSI-1/dpms
```

期望：`gpu-initialized: 1`，`revision: 610`，**没有** `Inconclusive highest_bank_bit`，**没有** `failed to create dri2 screen`，有 `GNOME Shell started`。
