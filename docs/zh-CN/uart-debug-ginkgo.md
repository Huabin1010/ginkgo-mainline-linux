**语言：** [English](../uart-debug-ginkgo.md) | 简体中文

# ginkgo UART 调试（主线启动失败必备）

无串口时只能看到「回 fastboot」，抓不到 panic / earlycon。

## 接线（只用 1.8V）

参考图：`docs/images/uart-ttl/ginkgo-usb-ttl-wiring.png`

| 手机测试点 | 信号 | USB-TTL |
|-----------|------|---------|
| **TP0003** | DBG_UART_TX (GPIO16) | **RX** |
| **TP0012** | DBG_UART_RX (GPIO17) | **TX** |
| 螺丝孔 GND | GND | **GND** |

**必须用 1.8V 逻辑电平**，不要接 3.3V/5V。

测试点只有 0.3–0.8 mm，杜邦线手扶焊不住。用 **`漆包线 0.1mm`**（或 30AWG 航模电子线）焊到焊盘：先刮掉/烫掉端头绝缘漆再上锡；另一头接到杜邦线，再插 USB-TTL。采购和焊接细节见 [UART 详细指南](ginkgo-usb-ttl-uart.md)。

## 主机端抓 log

```bash
# 安装
sudo apt install picocom

# 插上 USB-TTL 后确认设备
ls -l /dev/ttyUSB* /dev/ttyACM*

# 115200 8N1
picocom -b 115200 /dev/ttyUSB0

# 或保存到文件
picocom -b 115200 /dev/ttyUSB0 | tee backup/ginkgo/logs/uart-$(date +%H%M%S).log
```

也可：

```bash
./scripts/capture-uart.sh
```

## 测试主线时的步骤

1. 打开 picocom / capture-uart（先开串口再开机）
2. recovery 里刷主线 boot：
   ```bash
   adb push out/boot.img /tmp/boot.img
   adb shell "dd if=/tmp/boot.img of=/dev/block/by-name/boot bs=4M && sync"
   adb reboot
   ```
3. 把串口从 `Uncompressing Linux` 到 panic 的全文发我

## 当前 boot 参数

```
console=ttyMSM0,115200n8
earlycon=msm_serial_dm,0x4a90000
```

UART 控制器：`uart4 @ 0x04a90000`
