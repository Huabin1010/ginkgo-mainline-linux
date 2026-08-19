**语言：** [English](../ginkgo-touch-complete-2026-08-17.md) | 简体中文

# Redmi Note 8 (ginkgo) 主线触控：从红色 NO TOUCH DEVICE 到点屏出点

> 设备：Xiaomi Redmi Note 8 · codename **ginkgo** · SoC **SM6125 (trinket)** · 序列号 `<serial>`  
> 触控 IC：**Novatek NT36672A** · 总线 **SPI**（不是 I2C）· 1080×2340  
> 控制器：QUP v3 **SE2** = 主线 `&spi2` = `spi@4a88000`  
> 面板：Tianma NT36672A（与触控同系列 IC，显示走 DSI，触控走独立 SPI）  
> 栈：下游 `nt36xxx_spi_c3j` 移植到主线 7.0 + 内置 `novatek_ts_tianma_fw.bin`  
> 验收日：2026-08-17 晚 · 用户确认「ok 非常的完美」  
> 前置：同日显示 P3 已出图，见 [ginkgo-display-complete-2026-08-17.md](./ginkgo-display-complete-2026-08-17.md)

**关联文档 / skill**

| 文档 | 内容 |
|------|------|
| [ginkgo-display-complete-2026-08-17.md](./ginkgo-display-complete-2026-08-17.md) | 同日 DPU→DSI 出图（触控的前置） |
| [ginkgo-mainline-bringup-chronicle.md](./ginkgo-mainline-bringup-chronicle.md) | 全机 bring-up 时间线 |
| [firmware/ginkgo/README.md](../firmware/ginkgo/README.md) | 本机提取的 Tianma 固件 |
| [reference/README.md](../reference/README.md) | 下游驱动 / DTS 对照 |
| [ginkgo-touch-test.sh](../scripts/ginkgo-touch-test.sh) | Agent 触控回归 SOP |
| [usb-connect.sh](../scripts/usb-connect.sh) | 每次 reboot 后重配 RNDIS |
| [reboot-fastboot.sh](../scripts/reboot-fastboot.sh) | 从 Ubuntu 进 fastboot |

---

## 0. 一句话结论

触控最终能出点，不是因为「再 defer 几次 probe」或「再等面板偏压」，而是因为叠在一起的 **三件真事**：

1. **电气上 CS 是低有效（SPI_MODE_0）**。下游 DTS 写了 `spi-cs-high`，但下游驱动立刻 `spi->mode = SPI_MODE_0` 把它清掉。主线若保留 DT 的 `SPI_CS_HIGH`（`mode=4`），MISO 会一直读到全 `0xFF`。  
2. **NT36672A 是 host-download 型 SPI 芯片**：chip ID 只能证明 bootloader 还活着；真正报点要在开机后把 `novatek_ts_tianma_fw.bin` 下到 SRAM。  
3. **`nt36xxx.c` 里 `#undef CONFIG_FB`，固件文件没 undef**，两边看到的 `struct nvt_ts_data` 布局差一截。`fw_update` 读到的 `ts->mmap` 是 NULL，固件下载 Oops（`ESR 0x96000005`，`ldr w0,[x0]`）。对齐之后：**固件 377ms 下完，PID=`591F`，用户点屏出点。**

中间还踩过：sleep pinctrl 把 MOSI/MISO/CLK/CS 切回 gpio（也会全 `0xFF`）、GENI SPI 无 GPI DMA 时 63KB 一次传输会走 DMA 路径、`fastboot boot` 卡在 Sending、Oops 之后 sshd 死掉必须人进 fastboot。

---

## 1. 心得（给下一次 SPI 触控 / 下游驱动移植）

### 1.1 主线 `novatek-nvt-ts` 对 ginkgo 没用

主线 `drivers/input/touchscreen/novatek-nvt-ts.c` 是 **I2C-only**。ginkgo 这块 NT36672A 挂在 **SPI** 上，compatible 也不是 `novatek,nvt-ts`。不要在 DTS 里硬套 I2C 节点「试试看」。

正确来源：

```
reference/downstream/drivers/nt36xxx_spi_c3j/
reference/downstream/dts/xiaomi/ginkgo/ginkgo-trinket-touchscreen.dtsi
firmware/ginkgo/novatek_ts_tianma_fw.bin
```

### 1.2 读芯片文档之前，先读「驱动实际写了什么」

下游 DTS 明明有 `spi-cs-high`。若只信 DT，会把 CS 做成高有效。实机 `dmesg`：

| 驱动写法 | `mode=` | 含义 | 结果 |
|----------|---------|------|------|
| 主线曾：`mode &= ~(SPI_CPOL\|SPI_CPHA)` 保留 DT | **4** = `SPI_CS_HIGH` | CS 高有效 | 全 `0xFF` |
| 下游：`mode = SPI_MODE_0` | **0** | CPOL=0 CPHA=0，CS **低**有效 | chip ID `0x0A 00 00 72 66 03` |

DT 里的 `spi-cs-high` 是死配置；驱动覆盖才是电气真相。移植时 **对照 probe 里 `spi_setup` 前后的 `mode` 值**，不要只 diff dtsi。

全 `0xFF` 的含义几乎总是：**CS 没选中 / 时钟没出 / 脚不是 SPI 功能**，MISO 被上拉。不是「芯片坏了」，也不是「固件没加载」（那时候还没到固件）。

### 1.3 Qualcomm GENI SPI：sleep pinctrl 和 DMA 都是坑

QUP SE 的 `spi-geni-qcom` 在 `runtime_suspend` / `resources_off` 时会切到 `pinctrl-1`（sleep）。若 sleep 状态是 `gpio` 而不是 `qup02`，下一次传输 MOSI/MISO/CLK/CS 全是 GPIO，读数全 `0xFF`。ginkgo 的修法：**default 和 sleep 都指向 `qup_spi2_active`**。

`geni_can_dma()`：传输长度 **大于 FIFO**（本机大约 64 字节量级）就宣称「能 DMA」。下游靠 GPI DMA（`dmas`）。主线 ginkgo 删了 `dmas`/`dma-names`（FIFO-only）。63×1024 字节一次 `spi_sync` 会走进 DMA 路径，控制器没有可用 DMA 通道时表现为卡住或后续 Oops。  
当前 `NVT_TRANSFER_LEN = 32`，把固件按 32 字节切块走 FIFO。这是带约束的正确做法，不是「随便改小」。

### 1.4 多编译单元必须看到同一份结构体

下游驱动把 FB notifier 编进 `struct nvt_ts_data`（`#if CONFIG_FB`）。主线没有 `FB_EARLY_EVENT_BLANK`，`nt36xxx.c` 在 **include 头文件之前** `#undef CONFIG_FB`。`nt36xxx_fw_update.c` 只 `#include "nt36xxx.h"`，内核 `CONFIG_FB=y`（msmdrmfb），于是：

```
nt36xxx.c     的 ts->mmap 偏移 = A
fw_update.c   的 ts->mmap 偏移 = A + sizeof(workqueue*) + work_struct + notifier_block
```

于是出现过：

- 固件文件名打出来是空的（读错了 `boot_update_firmware_name`）→ `request_firmware` 返回 `-EINVAL`（-22）  
- `ts->hw_crc` 读成 0，走了无 HW CRC 的下载路径  
- `ts->mmap` 读成 NULL → `nvt_write_addr(ts->mmap->EVENT_BUF_ADDR | …)` → Oops

**铁律：** 改变结构体布局的 `#undef` / `#define` 必须放进 **头文件**，所有 `.c` 共用。不要只在某一个 `.c` 里 undef。

### 1.5 Chip ID 通 ≠ 能报点

`nvt_ts_check_chip_ver_trim` 只跟 bootloader 说话。NT36672A 要 host 把 FW 写进 SRAM，再 `nvt_boot_ready()`。probe 成功、`/dev/input/event1` 出现、IRQ 计数仍是 0 —— 在没人摸屏时正常；若摸了仍是 0，先看固件有没有 `Update firmware success`。

### 1.6 触控和面板共用电源，失败路径不要乱关

| 电源 | 触控 DT | 面板 DT | 注意 |
|------|---------|---------|------|
| L9A 1.8V | `touch_vddio` | `vddio` | IO，两边都要 |
| LCDB LDO (+) | `touch_lab` | `vddpos` | 模拟偏压，**共用** |
| LCDB NCP (-) | `touch_ibb` | `vddneg` | 同上 |

probe 失败时 `regulator_disable(lab/ibb)` 可能在面板还没 enable 时把 LCDB 关掉。lab/ibb 已改 `regulator_get_optional`；不要在触控错误路径上长时间关掉 LCDB。

### 1.7 调试通道：Oops 之后 SSH 可能死

固件线程 Oops 不一定 panic 整机，但 sshd / gadget 可能不再响应。ping 通、SSH 拒绝 = 内核还在、用户态半死。这时 **不要死等 connect.sh**，让用户按键进 fastboot，用 `fastboot boot out/boot.img`（**不要** `fastboot flash boot`）。

每次成功 `fastboot boot` 之后：

```bash
./scripts/usb-connect.sh
```

`enx*` 每次都会变。

### 1.8 一次只改一个变量

这次有效的顺序是：

1. 脚 mux 成 `qup02`（否则不必谈协议）  
2. CS 极性改成 MODE_0（否则不必谈 chip ID）  
3. 结构体对齐（否则不必谈固件）  
4. 传输长度 ≤ FIFO（否则大块下载走坏 DMA）  

把 defer、等面板、改 CS、改 pinctrl 叠在同一次构建里，日志会对不上「到底哪一刀生效」。

---

## 2. 目标、验收、非目标

### 2.1 目标

在主线 Linux 上走完整 **QUP SE2 SPI → NT36672A bootloader → host 下载 Tianma FW → input 子系统**，用户手指点屏可见。

### 2.2 量化验收（2026-08-17 实测）

| 指标 | 成功阈值 | 实测 |
|------|----------|------|
| SPI `mode` | 0（MODE_0） | `mode=0, max_speed_hz=8000000` |
| Chip ID | 非全 `0xFF`，命中 trim 表 | `0x0A 00 00 72 66 03`，「This is NVT touch IC」 |
| 固件 | `Update firmware success` | `<376682 us>`，无 Oops |
| FW 信息 | `nvt_get_fw_info` / PID | `FW type is 0x01`，`PID=591F` |
| 输入节点 | `NVTCapacitiveTouchScreen` | `/dev/input/event1`，ABS 0..1079 × 0..2339 |
| 可视化 | 绿色 **TOUCH OK**，点屏出点 | 用户：「ok 非常的完美」 |
| Oops | `dmesg \| grep Oops` 为 0 | 对齐后 0 |

### 2.3 非目标（本次不做）

- 手势唤醒 / 双击亮屏产品化  
- MP 产线测试（`NVT_TOUCH_MP=0`）  
- `/proc` 调试节点（`NVT_TOUCH_PROC=0`、`NVT_TOUCH_EXT_PROC=0`）  
- Focaltech 另一套 IC（下游 DTS 里兼容，本机是 Tianma Novatek）  
- 把 `fastboot boot` 固化进 boot 分区（需用户明确要求）  
- WiFi

---

## 3. 硬件与引脚（对照下游）

### 3.1 总线

下游节点：`&qupv3_se2_spi` → 主线 `&spi2`（`spi@4a88000`）。

| 信号 | TLMM | 功能 |
|------|------|------|
| MOSI | gpio6 | `qup02` |
| MISO | gpio7 | `qup02` |
| CLK  | gpio8 | `qup02` |
| CS   | gpio9 | `qup02` |
| RESET | gpio87 | gpio，输出高 |
| IRQ   | gpio88 | gpio，上拉输入，上升沿 |

`spi-max-frequency = <8000000>`（8 MHz），与下游一致。

### 3.2 为何删 DMA

主线 `sm6125.dtsi` 里 spi2 带 `dmas` / `dma-names`（GPI）。ginkgo 上 GENI GPI DMA 未作为触控路径验证；为避免 probe 卡在 DMA，DTS 里：

```dts
/delete-property/ dmas;
/delete-property/ dma-names;
```

配合 `NVT_TRANSFER_LEN=32` 走 FIFO。

### 3.3 pinctrl（决定性细节）

```dts
qup_spi2_active: qup-spi2-active-state {
    pins = "gpio6", "gpio7", "gpio8", "gpio9";
    function = "qup02";
    drive-strength = <6>;
    bias-disable;
};

&spi2 {
    pinctrl-0 = <&qup_spi2_active>;
    pinctrl-1 = <&qup_spi2_active>;   /* sleep 也保持 qup02 */
    pinctrl-names = "default", "sleep";
    ...
};
```

早期 sleep 切回 `gpio` 时：gpio6–9 的 `function` 变成 0，MISO 全 `0xFF`。注释写在 DTS 里，不要「顺手」改回标准 sleep-gpio。

### 3.4 固件

从本机 Android 分区提取，见 `firmware/ginkgo/README.md`：

| 文件 | 大小 | 用途 |
|------|------|------|
| `novatek_ts_tianma_fw.bin` | 118784 B | **本机 Tianma，实际使用** |
| `novatek_ts_tianma_mp.bin` | 118784 B | MP 测试，未启用 |
| `novatek_ts_ebbg_fw.bin` | 118784 B | EBBG 面板 variant，本机不用 |

内核 fragment：

```
CONFIG_SPI_QCOM_GENI=y
CONFIG_TOUCHSCREEN_NT36XXX_SPI=y
CONFIG_EXTRA_FIRMWARE="novatek_ts_tianma_fw.bin"
CONFIG_EXTRA_FIRMWARE_DIR="…/firmware/ginkgo"
```

实机 `fw_need_write_size = 110592 (0x1b000)`（bin 末尾 NVT end flag 之前的有效载荷）。

ABL 传下来的 cmdline 里有 `dsi_nt36672a_tianma_vid_display`，所以 `CHECK_TOUCH_VENDOR` 能 `strstr(..., "tianma")` 命中。不要依赖自己 mkbootimg 的短 cmdline 里也有 tianma —— 缺了会走 default tianma，本机碰巧还对。

---

## 4. 软件落地位置

| 项 | 路径 |
|----|------|
| 驱动 | `linux/drivers/input/touchscreen/nt36xxx/`（`nt36xxx.c` / `nt36xxx_fw_update.c` / `nt36xxx.h` / `nt36xxx_mem_map.h`） |
| Kconfig | `TOUCHSCREEN_NT36XXX_SPI` |
| DTS | `linux/arch/arm64/boot/dts/qcom/sm6125-xiaomi-ginkgo.dts` 的 `&spi2` + pinctrl |
| 固件 | `firmware/ginkgo/novatek_ts_tianma_fw.bin`（编进 vmlinux） |
| 可视化 | `scripts/ginkgo-touch-test.py` → overlay `/usr/local/sbin/ginkgo-touch-test.py` |
| 下游参考 | `reference/downstream/drivers/nt36xxx_spi_c3j/` |

主线 API 移植要点（相对下游 4.14 风格）：

- `spi->master` → `spi->controller`  
- `remove` 改为 `void`  
- `do_gettimeofday` → `ktime_get`  
- `of_get_named_gpio_flags` → `of_get_named_gpio`  
- `late_initcall`（给 regulator / SPI 一点时间）  
- stub `set_lcd_reset_gpio_keep_high`（面板 reset 归 DRM）  
- 关掉 MP / EXT_PROC / PROC；保留 `WAKEUP_GESTURE` 只为 regulator 代码路径  
- 全局 `struct nvt_ts_data *ts` **不能删**（fw_update 用 `extern`）

---

## 5. 失败实验时间线（避免再走一遍）

按时间，不是按「看起来合理的顺序」。

### 5.1 只有 gpio-keys：红色 NO TOUCH DEVICE

用户态测试画 fb0。当时 `/dev/input` 只有音量键。横幅红字 **NO TOUCH DEVICE** / **DRIVER NOT PROBED**。这是「没设备节点」，不是「有设备但不报点」。

### 5.2 第一次接上驱动：chip ID 全 `0xFF`，`error -22`

pinmux 仍是 gpio。读数全 1。probe `late_initcall` 约 3.0s，面板 init 约 3.8s，容易误判成「偏压没稳」。**先 `cat` gpio6–9 的 function。**

### 5.3 sleep pinctrl 改成 qup02 之后：仍全 `0xFF`

脚已经是 `qup02`。真正的锅换成 CS 极性。此时 `mode=4`。

### 5.4 误删全局 `ts`：vmlinux 链接失败

`undefined reference to ts`。脚本若仍用 **旧 Image.gz** 打包 `boot.img`，会把上一版内核 boot 上去，日志和源码对不上。打包前确认 `Image.gz` 时间戳 / 大小变了。

### 5.5 `EPROBE_DEFER` 20 次

想法：等面板 LCDB。实机后来证明 CS 对了之后 **第一次 probe（~3.2s）就能读到 chip ID**，不需要 defer。Defer 可以留着当保险，但不要当成根因。

### 5.6 CS 改对之后：有 `NVTCapacitiveTouchScreen`，固件文件名为空，`-22`

`request_firmware("")` → `-EINVAL`。当时以为是 `BOOT_UPDATE_FIRMWARE_NAME` 和 `EXTRA_FIRMWARE` 文件名不一致。真正原因是 **结构体错位**，`boot_update_firmware_name[]` 读到的是别的字段。硬编码 tianma 文件名当字符串字面量能绕过「空名字」，把 bin 加载进来，然后在 parser 之后 Oops。

### 5.7 固件 bin 已加载：parser 打完 partition=15，然后 Oops

```
Workqueue: nvt_fwu_wq Boot_Update_Firmware
pc : nvt_update_firmware+0x954
Code: ... f9418800 (b9400000)    ; ldr x0,[x0,#784]; ldr w0,[x0]
x0=0
ESR: 0000000096000005           ; data abort, translation fault
```

反汇编对应：

```c
nvt_write_addr(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_RESET_COMPLETE, 0x00);
```

`ts->mmap == NULL`。同时误走了 `hw_crc==0` 的 `nvt_download_firmware()`（NT36672A 实际 `hw_crc=1`）。根因：CONFIG_FB 布局不一致。

Oops 后 sshd 经常挂掉。ping 还在。必须人进 fastboot 再 `fastboot boot`。

### 5.8 63KB 传输（次要，被 Oops 盖住）

`NVT_TRANSFER_LEN = 63*1024` 是给 DMA 准备的。FIFO-only 控制器上应视为有害。即使结构体对齐了，也应保持小块。当前 32。

---

## 6. 三个决定性修复（代码级）

### 6.1 CS = SPI_MODE_0，DTS 去掉 `spi-cs-high`

`nt36xxx.c` probe：

```c
ts->client->bits_per_word = 8;
/* 下游用 MODE_0 覆盖 DT spi-cs-high。mode=4 在 ginkgo 上 MISO 全 0xFF。 */
ts->client->mode = SPI_MODE_0;
```

DTS `touchscreen@0` **不要** `spi-cs-high`。

### 6.2 头文件统一 `#undef CONFIG_FB`

`nt36xxx.h` 在 `struct nvt_ts_data` **之前**：

```c
/* 必须在头文件里 undef，让 nt36xxx.c 与 nt36xxx_fw_update.c 布局一致。 */
#undef CONFIG_FB
```

`nt36xxx.c` 里原来的 `#undef` 可以留着当注释性防御，但不能只留在 `.c`。

### 6.3 小块 SPI + 明确固件名

`nt36xxx.h`：

```c
#define NVT_TRANSFER_LEN  (32)
#define BOOT_UPDATE_TIANMA_FIRMWARE_NAME  "novatek_ts_tianma_fw.bin"
```

probe 里在排队 FW work 前：

```c
strscpy(ts->boot_update_firmware_name, BOOT_UPDATE_TIANMA_FIRMWARE_NAME,
        sizeof(ts->boot_update_firmware_name));
```

`Boot_Update_Firmware` 若名字为空则回退到同一字面量（防错位复发）。

---

## 7. 验收日志（完整 NVT 序列）

结构体对齐后的一次 `fastboot boot`（2026-08-17 23:24 `boot.img`）：

```
[    2.839] nvt_driver_init: start
[    2.850] TP info: [Vendor]tianma [IC]nt36672a
[    2.882] nvt_ts_probe: start
[    2.893] mode=0, max_speed_hz=8000000
[    2.907] novatek,reset-gpio=599
[    2.921] novatek,irq-gpio=600
[    2.934] SWRST_N8_ADDR=0x03F0FE
[    2.947] SPI_RD_FAST_ADDR=0x03F310
[    2.961] get/put regulator : 1
[    3.186] buf[1]=0x0A, buf[2]=0x00, buf[3]=0x00, buf[4]=0x72, buf[5]=0x66, buf[6]=0x03
[    3.227] This is NVT touch IC
[    3.286] request irq 102 succeed
[    3.300] fw name='novatek_ts_tianma_fw.bin'
[    3.333] nvt_ts_probe: end
[    8.537] filename is novatek_ts_tianma_fw.bin
[    8.555] fw_need_write_size = 110592(0x1b000), NVT end flag
[    8.576] ovly_info=0 ilm_dlm_num=2 ovly_sec_num=0 info_sec_num=13 partition=15
[    8.978] Update firmware success! <376682 us>
[    8.996] FW type is 0x01
[    9.008] PID=591F
```

用户态：

```
/dev/input/event0   gpio-keys                     no
/dev/input/event1   NVTCapacitiveTouchScreen      YES  0..1079 x 0..2339
```

IRQ 在无人触摸时为 0 是正常的：

```
102:  0  0  0  0  0  0  0  0  msmgpio  88 Edge  NVT-ts
```

点屏后该行应增加。可视化：`python3 /usr/local/sbin/ginkgo-touch-test.py`（音量- 退出，音量+ 清屏）。

Chip ID `0x0A,0x00,0x00,0x72,0x66,0x03` 命中 `nt36xxx_mem_map.h` 里：

```c
{.id = {0x0A, 0xFF, 0xFF, 0x72, 0x66, 0x03}, .mask = {1, 0, 0, 1, 1, 1},
 .mmap = &NT36672A_memory_map, .hwinfo = &NT36672A_hw_info},  /* hw_crc = 1 */
```

---

## 8. 可视化测试怎么用

主机（手机已 SSH）：

```bash
./scripts/usb-connect.sh
SSHPASS=$GINKGO_ROOT_PASSWORD sshpass -e ssh -b 192.168.7.1 root@192.168.7.2 \
  'setsid python3 /usr/local/sbin/ginkgo-touch-test.py </dev/null >/tmp/touch-test.log 2>&1 &'
```

不要在 SSH 前台直接跑（断开 SSH 可能把进程带走）。`setsid` + 重定向。

脚本在 `scripts/ginkgo-touch-test.py`，打进 initramfs overlay。识别规则包含名字里的 `nvt` / `touch`，以及 ABS_MT。

---

## 9. 已知遗留（触控已通，不必挡报点）

| 项 | 说明 |
|----|------|
| `SPI driver NVT-ts has no spi_device_id` | 缺 `spi_device_id` 表，无害 |
| 若干 `-Wmissing-prototypes` | 下游风格，未清理 |
| `NVT_TRANSFER_LEN=32` | FIFO 安全；若以后启用 GPI DMA 可再加大 |
| 手势 / 休眠 / 双击 | `WAKEUP_GESTURE` 编译开着，产品行为未验收 |
| lab/ibb 与面板共用 | 不要在触控 remove 路径关死 LCDB |
| `fastboot boot` 未固化 | 掉电/正常 reboot 会回到分区里的旧内核 |
| WiFi | P4 剩余项 |
| ttyMSM0 仍 deferred | 与触控无关 |

---

## 10. 给 Agent 的 SOP（触控回归）

1. 每次 reboot / `fastboot boot` 后先 `./scripts/usb-connect.sh`  
2. `dmesg | grep NVT-ts`  
   - 要看到 `mode=0`  
   - 要看到非 `0xFF` 的 chip ID  
   - 要看到 `Update firmware success`  
   - **不要**出现 `Internal error: Oops` / `mmap` 空指针  
3. `cat /sys/class/input/event*/device/name` 含 `NVTCapacitiveTouchScreen`  
4. 可视化用 `ginkgo-touch-test.py`；横幅应是绿 **TOUCH OK**  
5. 若又全 `0xFF`：先查 gpio6–9 是否 `qup02`，再查 `mode=` 是不是又变成 4  
6. 若 chip ID 对、没 success：查 Oops、查 `NVT_TRANSFER_LEN`、查两个 `.c` 是否都看到 `#undef CONFIG_FB`  
7. 验证用 `fastboot boot out/boot.img`，**不要** `fastboot flash boot`  
8. 链接错误 `undefined reference to ts` = 全局 `ts` 丢了，不要用旧 Image 混打包  

刷机循环：

```bash
source scripts/env.sh
make -C "$KERNEL_SRC" O="$KBUILD_OUTPUT" -j"$(nproc)" Image.gz dtbs
cp -f "$KBUILD_OUTPUT/arch/arm64/boot/Image.gz" out/Image.gz
cp -f "$KBUILD_OUTPUT/arch/arm64/boot/dts/qcom/sm6125-xiaomi-ginkgo.dtb" out/sm6125-xiaomi-ginkgo.dtb
./scripts/build-bootimg.sh
# 手机在 Ubuntu 且 SSH 好：
./scripts/reboot-fastboot.sh
fastboot boot out/boot.img
# Oops 后 SSH 死了：让用户按键进 fastboot，再 fastboot boot
```

---

## 11. 和显示 bring-up 的对照

| | 显示 P3 | 触控（本文件） |
|--|---------|----------------|
| 假阳性 | 背光亮、DCS complete、INTF 60fps | event 节点在、IRQ=0、chip ID 在 |
| 真证伪 | DSI TPG 上屏；FIFO `0x1010` | `mode=0` + 非 0xFF ID + `Update firmware success` + 手指出点 |
| 主线默认值坑 | prog fetch 24 行 vs VFP 10 | `spi-cs-high` DT vs 驱动 MODE_0；CONFIG_FB 结构体 |
| 共用硬件 | LCDB / L9A / GPIO90 reset | 同一套 LCDB / L9A；reset 是 GPIO87 |
| 请用户看什么 | FIFO 健康后的品红 | 绿色 TOUCH OK 后点屏 |

显示必须先通：测试程序画在 `/dev/fb0` 上。触控不依赖 DRM 合成路径，但依赖 LCDB 模拟电和 SPI 脚。

---

*记录结束。触控验收日期 2026-08-17。*
