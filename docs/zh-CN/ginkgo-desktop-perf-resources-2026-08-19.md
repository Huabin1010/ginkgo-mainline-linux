**语言：** [English](../ginkgo-desktop-perf-resources-2026-08-19.md) | 简体中文

# Redmi Note 8 (ginkgo) 桌面卡顿、Resources 温度与 GPU 图

> 设备：Xiaomi Redmi Note 8 · codename **ginkgo** · SoC **SM6125 (trinket)** · 序列号 `<serial>`  
> 系统：主线 Linux 7.0 + Ubuntu 26.04 LTS arm64，rootfs 在 userdata  
> 验收日：2026-08-19 02:36 · 用户确认 CPUFreq 后「整体好了很多，但还是很卡」；随后补 TSENS / GPU sysfs / GSK，Resources 侧已能读到温度和 GPU 占用  
> 前置：Adreno 610 桌面已可见。见 [ginkgo-gpu-desktop-2026-08-19.md](./ginkgo-gpu-desktop-2026-08-19.md)。

**关联文档 / skill**

| 文档 | 内容 |
|------|------|
| [ginkgo-gpu-desktop-2026-08-19.md](./ginkgo-gpu-desktop-2026-08-19.md) | 原生 GPU 起来、桌面可见 |
| [ginkgo-ubuntu-desktop-2026-08-19.md](./ginkgo-ubuntu-desktop-2026-08-19.md) | 装 GNOME / 当时黑屏 |
| [ginkgo-mainline-bringup-chronicle.md](./ginkgo-mainline-bringup-chronicle.md) | 全机时间线 |
| [usb-connect.sh](../scripts/usb-connect.sh) | 每次 reboot 后重配 RNDIS |
| [reboot-fastboot.sh](../scripts/reboot-fastboot.sh) | 从 Ubuntu 进 fastboot |

验证仍用 `fastboot boot out/boot.img`，**不要** `fastboot flash boot`。

---

## 0. 一句话结论

卡顿的第一刀不是 GPU「没动」，而是 **八核都没有 CPUFreq**：小核/大核停在 bootloader 投票上，调度器还把交互任务派到当时更慢的大核。补上 OSM 之后用户确认整体好了很多。

Resources 里看不到 Sensors 温度、GPU 占用一条直线，是 **用户态只认 x86/AMD 的 sysfs 名字**，不是传感器或 GPU 没工作。已经按它的约定补了 `cpu-thermal` 和 `gpu_busy_percent` / `freq1_input`。

第一次带 BWMON 的 `fastboot boot` 会卡死 USB：trinket 的 CPU bwmon 不能照搬 SM6115 的 `0x01b8e300`。这块目前 **disabled**。

---

## 1. 用户看到的三件事

| 现象 | 实际原因 |
|------|----------|
| 点图标、进桌面、开应用、返回都卡 | 无 `cpufreq`；大核当时比小核还慢 |
| Resources Sensors 没有 CPU 温度 | SM6125 原先没有 TSENS；就算有，Resources 也只认 type **`cpu-thermal`** |
| Resources GPU 占用/时钟完全不动 | 它读的是 AMD 路径：`card0/device/gpu_busy_percent` 和 `hwmon/*/freq1_input`；msm 原来没有 |

机上证据（CPUFreq 之前）：

```
/sys/devices/system/cpu/cpu0/cpufreq   不存在（8 核都没有）
同一段 Python：小核 ~2.6s，大核 ~5.7s
gnome-shell ~35–41% CPU，GSK_RENDERER=ngl，缩放 2
GPU 其实在升降频（320–950 MHz，trans_stat 有切换）
wa=0，不是 eMMC 拖死
```

GNOME Resources（nokyan/resources）CPU 温度只认：

- hwmon 名：`zenpower` / `coretemp` / `k10temp` / `ibmpowernv`
- thermal zone type：`cpu-thermal` / `x86_pkg_temp` / `acpitz`

GPU「Other」（msm）只读：

```
/sys/class/drm/card0/device/gpu_busy_percent
/sys/class/drm/card0/device/hwmon/hwmon?/freq1_input
```

`card0/device` 是 DPU 平台设备（`5e01000.display-controller`），不是 `5900000.gpu`。sysfs 必须挂在这台设备上。

---

## 2. 做了哪些事

### 2.1 CPUFreq（卡顿第一刀，用户已确认有效）

SM6125 DTS 原先没有 OSM。Kryo 260 的 OSM 与 SM6115 同寄存器：

- `cpufreq@f521000`：`qcom,sm6125-cpufreq-hw`，`0xf521000` / `0xf523000`
- CPU 节点挂 `qcom,freq-domain` + `clocks`
- `CONFIG_ARM_QCOM_CPUFREQ_HW=y`
- yaml：`qcom,sm6125-cpufreq-hw`

验收（`fastboot boot` 后）：

| 集群 | governor | 当时频率 | 范围 |
|------|----------|----------|------|
| 小核 cpu0–3 | schedutil | 1804 MHz | 300–1804 |
| 大核 cpu4–7 | schedutil | 2016 MHz | 300–2016 |

用户反馈：**整体好了很多，但还是很卡。**

### 2.2 TSENS + `cpu-thermal`（Resources Sensors）

下游 `trinket.dtsi`：`tsens@4410000`，TM `0x4411000`、SROT `0x4410000`，IRQ 275 / 190。主线 `init_common()` 按 **index 0 = TM、index 1 = SROT** 解析，顺序与下游 `reg-names` 相反，地址一致。

落地：

- `tsens0@4411000`：`qcom,sm6115-tsens`, `qcom,tsens-v2`，16 传感器
- `qfprom@1b40000`：给 `CONFIG_QCOM_TSENS` 依赖的 QFPROM
- `CONFIG_QCOM_TSENS=y`、`CONFIG_NVMEM_QCOM_QFPROM=y`
- thermal-zones 里 **必须有一个就叫 `cpu-thermal`**（tsens0 传感器 6，trinket 第一颗 Kryo gold）

额外 zone（Resources 不用，调试用）：`cpu0123-thermal`、`gpu-thermal`、`mapss-thermal`。

critical trip 先改成 **`hot`**，避免未校准读数触发 `orderly_poweroff`。

机上读数（2026-08-19 02:35）：

```
mapss-thermal     41600
cpu-thermal       43800
cpu0123-thermal   46100
gpu-thermal       40900
```

单位 millicelsius，约 41–46 °C，合理。

### 2.3 GPU 占用 / 时钟（Resources GPU 图）

`linux/drivers/gpu/drm/msm/`：

- `gpu_busy_percent`：`DEVICE_ATTR_RO`，挂在 DRM 父设备上，路径即 Resources 要的 `card0/device/gpu_busy_percent`
- hwmon 名 `adreno`，自定义 `freq1_input`（主线 `hwmon` 没有 `hwmon_freq` 类型，跟 amdgpu 一样手写属性）
- 频率来自 `msm_gpu_get_freq()`（idle 时用 shadow `idle_freq`）

**不要从 sysfs 直接读 GMU `POWER_COUNTER`。** 第一次实现调用 `msm_devfreq_get_dev_status()` → `a6xx_gpu_busy()` → `gmu_read64()`，和 IFPC 竞态时会把总线卡死，`cat gpu_busy_percent` 挂住。改为返回 governor 最近一次采样（`devfreq.last_busy_percent`，50 ms 一次）。

空闲时占用接近 0、时钟 **320 MHz** 是正常的。滑动桌面采到过 3%～44%，时钟到过 745 MHz。

### 2.4 GPU 内存带宽 + GSK

- GPU 节点：`interconnects` `MASTER_GRAPHICS_3D → SLAVE_EBI_CH0`，`interconnect-names = "gfx-mem"`
- OPP 加 `opp-peak-kBps`（adreno 已调用 `dev_pm_opp_of_find_icc_paths`）
- overlay：`rootfs-overlay/etc/environment.d/99-ginkgo-gnome.conf` 从 `GSK_RENDERER=ngl` 改为 **`gl`**（ngl 是给 llvmpipe 写的）

当前会话：`gnome-shell` 环境已是 `GSK_RENDERER=gl`。

### 2.5 CPU BWMON：先关掉

下游 trinket：

```
reg = <0x01b8e200 0x100>, <0x01b8e100 0x100>;  /* base, global_base */
IRQ 421
```

照抄 SM6115 `pmu@1b8e300` + `qcom,sdm845-bwmon` 后，`bwmon_start()` 写 ENABLE@0x2a0，**内核起不来、USB RNDIS 不出现**。用户只能按键回 fastboot。

当前 DTS：`pmu@1b8e200`，`reg = 0x01b8e200 / 0x600`，**`status = "disabled"`**。`CONFIG_QCOM_ICC_BWMON=y` 留着，节点未使能就不会 probe。

---

## 3. 改动文件（便于回归）

| 文件 | 作用 |
|------|------|
| `linux/arch/arm64/boot/dts/qcom/sm6125.dtsi` | OSM、TSENS、qfprom、thermal-zones、GPU ICC/OPP、bwmon disabled |
| `config/ginkgo.fragment` | `CPUFREQ_HW`、`TSENS`、`QFPROM`、`ICC_BWMON` |
| `linux/drivers/gpu/drm/msm/msm_drv.c` | `gpu_busy_percent` + adreno hwmon |
| `linux/drivers/gpu/drm/msm/msm_gpu_devfreq.c` | 缓存占用；`msm_gpu_get_freq()` |
| `linux/drivers/gpu/drm/msm/msm_gpu.h` / `msm_drv.h` | 声明与 `hwmon` 指针 |
| `rootfs-overlay/etc/environment.d/99-ginkgo-gnome.conf` | `GSK_RENDERER=gl` |
| yaml `cpufreq-qcom-hw.yaml` | `qcom,sm6125-cpufreq-hw` |

不要动：DSI 预取、触控 CS/`CONFIG_FB`、WiFi MSA、simplefb、HBB=14、zap `0x57515000`、SQE 用 linux-firmware。

---

## 4. 还没做完 / 仍卡的方向

1. **CPU bwmon 未启用** — 需要按下游 `0x01b8e200` + `0x01b8e100` 对上主线 `sdm845-bwmon` 或 `msm8998-bwmon`（双 region）再开。开之前只 `fastboot boot`。  
2. **剩余卡顿** — 1080×2340 + 缩放 2 + GNOME；GPU 带宽刚加上，体感要再滑几次才知道。  
3. Resources 必须 **关掉再开** 才会重新扫 sysfs。  
4. GPU 图在空闲时几乎是平的，要 **滑动/开窗口** 才会跳。

---

## 5. 不要做

| 不要 | 原因 |
|------|------|
| 再使能 `pmu@1b8e300` | 已确认会卡死启动 |
| 从 sysfs 再调 `gpu_busy()` / GMU 计数器 | IFPC 竞态，读寄存器挂死 |
| 把 thermal zone 起名叫 `cpu4-thermal` 之类 | Resources 不认，Sensors 仍空 |
| 把 `gpu_busy_percent` 挂在 `5900000.gpu` 上 | Resources 只看 `card0/device/` |
| 为「更顺」去改 DSI / HBB / zap | 像素和 GPU 合成已经对齐 |
| `fastboot flash boot` | 本仓库只 `fastboot boot` |

第一次 `fastboot boot` Sending 失败：**不要 usbreset**。`killall -9 fastboot` 再试。boot 后 USB 口会变，重跑 `connect.sh`。

---

## 6. 回归命令

```bash
# CPUFreq
cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq
cat /sys/devices/system/cpu/cpu4/cpufreq/scaling_cur_freq

# Resources 要的温度名
grep . /sys/class/thermal/thermal_zone*/type
cat /sys/class/thermal/thermal_zone1/temp   # cpu-thermal，约 40xxx

# Resources GPU
cat /sys/class/drm/card0/device/gpu_busy_percent
cat /sys/class/drm/card0/device/hwmon/hwmon*/freq1_input
# 滑动桌面时 busy 应非 0；空闲 freq 约 320000000

# GSK
tr '\0' '\n' < /proc/$(pgrep -n -u ginkgo gnome-shell)/environ | grep GSK
# 期望 GSK_RENDERER=gl

# 不应再出现
dmesg | grep -i bwmon
```

期望：小核/大核都有 `schedutil`；有 type `cpu-thermal` 且温度 millicelsius；`gpu_busy_percent` 连读 12 次不卡住；gnome-shell 为 `GSK_RENDERER=gl`。
