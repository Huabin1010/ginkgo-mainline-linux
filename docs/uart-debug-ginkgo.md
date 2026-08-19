**Language:** English | [简体中文](zh-CN/uart-debug-ginkgo.md)

# ginkgo UART debugging (required for mainline boot failures)

Without a serial port you only see “return to fastboot” and cannot capture panic / earlycon.

## Wiring (1.8V only)

See diagram: `docs/images/uart-ttl/ginkgo-usb-ttl-wiring.png`

| Phone test point | Signal | USB-TTL |
|-----------|------|---------|
| **TP0003** | DBG_UART_TX (GPIO16) | **RX** |
| **TP0012** | DBG_UART_RX (GPIO17) | **TX** |
| Screw-hole GND | GND | **GND** |

**You must use 1.8V logic levels.** Do not connect 3.3V/5V.

## Capturing logs on the host

```bash
# Install
sudo apt install picocom

# After plugging in the USB-TTL adapter, confirm the device
ls -l /dev/ttyUSB* /dev/ttyACM*

# 115200 8N1
picocom -b 115200 /dev/ttyUSB0

# Or save to a file
picocom -b 115200 /dev/ttyUSB0 | tee backup/ginkgo/logs/uart-$(date +%H%M%S).log
```

Alternatively:

```bash
./scripts/capture-uart.sh
```

## Steps when testing mainline

1. Open picocom / capture-uart (start the serial capture **before** powering on)
2. Flash the mainline boot image from recovery:
   ```bash
   adb push out/boot.img /tmp/boot.img
   adb shell "dd if=/tmp/boot.img of=/dev/block/by-name/boot bs=4M && sync"
   adb reboot
   ```
3. Send the full serial log from `Uncompressing Linux` through the panic

## Current boot parameters

```
console=ttyMSM0,115200n8
earlycon=msm_serial_dm,0x4a90000
```

UART controller: `uart4 @ 0x04a90000`
