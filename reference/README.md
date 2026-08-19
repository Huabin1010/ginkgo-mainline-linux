# ginkgo porting references

**Language:** English | [简体中文](README.zh-CN.md)

Collected from LineageOS 17.1 downstream and mainline, for side-by-side mainline work.

## Layout

```
reference/
├── mainline/          # Mainline ginkgo DTS (from torvalds/linux)
│   ├── sm6125-ginkgo.dts
│   ├── sm6125-ginkgo-common.dtsi
│   ├── sm6125.dtsi
│   └── sm6115-wifi-ref.dtsi   # Wi-Fi node reference (already on sm6115)
└── downstream/        # LineageOS/android_kernel_xiaomi_sm6125 @ lineage-17.1
    ├── dts/xiaomi/ginkgo/     # Full ginkgo device tree
    │   ├── display/dsi-panel-nt36672a-tianma-fhd-video.dtsi  ← panel timing / init
    │   ├── ginkgo-trinket-touchscreen.dtsi                   ← SPI touch
    │   ├── ginkgo-trinket-display.dtsi
    │   ├── ginkgo-trinket-pinctrl.dtsi
    │   └── ...
    ├── dts/qcom/trinket-*.dtsi  # trinket platform DTS
    └── drivers/
        ├── nt36xxx_spi_c3j/     # Novatek SPI touch (ported to linux/drivers/input/touchscreen/nt36xxx/)
        └── dsi-staging/         # Downstream DSI panel (init-sequence reference)
```

## Sources

| Content | Repository | Branch |
|---------|------------|--------|
| Downstream DTS + drivers | [LineageOS/android_kernel_xiaomi_sm6125](https://github.com/LineageOS/android_kernel_xiaomi_sm6125) | lineage-17.1 |
| Mainline DTS | [torvalds/linux](https://github.com/torvalds/linux) | master |

Clone method: sparse clone (`--filter=blob:none --sparse`), only the paths above (~2.2 MB).

## Subsystems

| Subsystem | Mainline | Downstream |
|-----------|----------|------------|
| Display | `mainline/sm6125-ginkgo-common.dtsi` | `downstream/dts/xiaomi/ginkgo/display/dsi-panel-nt36672a-tianma-fhd-video.dtsi` |
| Touch | `linux/drivers/input/touchscreen/nt36xxx/` (ported; points on 2026-08-17) | `downstream/drivers/nt36xxx_spi_c3j/` + `ginkgo-trinket-touchscreen.dtsi` |
| Wi-Fi | `mainline/sm6115-wifi-ref.dtsi` | Downstream Wi-Fi is in qcom-base / trinket-idp.dtsi (ICNSS) |

## Firmware

See `firmware/ginkgo/`. Wi-Fi firmware was pulled from the device `/vendor/firmware_mnt/image/` (`adb root`).
