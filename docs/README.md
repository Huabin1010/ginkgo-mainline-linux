# Documentation

**Language:** English | [简体中文](zh-CN/README.md)

English is the default. Chinese originals live in [`zh-CN/`](zh-CN/README.md). Every page has a language switch at the top.

## Start here

| Doc | What it is |
|-----|------------|
| [flash-guide.md](flash-guide.md) | Flash a GitHub Release (`boot.img` + empty dtbo) |
| [ginkgo-mainline-bringup-chronicle.md](ginkgo-mainline-bringup-chronicle.md) | Full bring-up timeline and current status |
| [mainline-ginkgo-porting-guide.md](mainline-ginkgo-porting-guide.md) | Hardware map, gaps vs mainline, staged plan |
| [ginkgo-usb-ttl-uart.md](ginkgo-usb-ttl-uart.md) | 1.8 V UART wiring (do not use the EDL pads) |
| [uart-debug-ginkgo.md](uart-debug-ginkgo.md) | Short UART cheat sheet |

## Milestones

| Date | Doc |
|------|-----|
| 2026-08-17 | [Display: magenta framebuffer on screen](ginkgo-display-complete-2026-08-17.md) |
| 2026-08-17 | [Touch: NT36672A SPI](ginkgo-touch-complete-2026-08-17.md) |
| 2026-08-18 | [fbcon boot log on panel](ginkgo-fbcon-boot-2026-08-18.md) |
| 2026-08-18 | [Wi-Fi: WCN3990 / ath10k_snoc](ginkgo-wifi-complete-2026-08-18.md) |
| 2026-08-19 | [Ubuntu GNOME desktop](ginkgo-ubuntu-desktop-2026-08-19.md) |
| 2026-08-19 | [Adreno 610 GPU](ginkgo-gpu-desktop-2026-08-19.md) |
| 2026-08-19 | [Desktop smoothness / Resources](ginkgo-desktop-perf-resources-2026-08-19.md) |
| 2026-08-19 | [Docker CE](ginkgo-docker-2026-08-19.md) |

## History and method

| Doc | What it is |
|-----|------------|
| [ginkgo-bringup-journal.md](ginkgo-bringup-journal.md) | Frozen early boot / init log (through 2026-08-08) |
| [ginkgo-journey-to-remote-ssh.md](ginkgo-journey-to-remote-ssh.md) | USB RNDIS + SSH |
| [mainline-boot-failure-analysis.md](mainline-boot-failure-analysis.md) | First “reboot to fastboot” days |
| [ginkgo-display-bringup-methodology.md](ginkgo-display-bringup-methodology.md) | LCDB / DCS / PHY / DPMS method |
| [ginkgo-dsi-err-status5-analysis.md](ginkgo-dsi-err-status5-analysis.md) | Historical `dsi_err status=5` |
| [postmarketOS-ginkgo-research.md](postmarketOS-ginkgo-research.md) | pmOS research notes |
| [ginkgo-phone-web-2026-08-19.md](ginkgo-phone-web-2026-08-19.md) | Public HTTPS in front of the phone (redacted) |

Raw register scratch notes stay Chinese-only: [`zh-CN/thinking.md`](zh-CN/thinking.md).

## Other READMEs

- [firmware/ginkgo](../firmware/ginkgo/README.md)
- [reference](../reference/README.md)
- [backup/ginkgo](../backup/ginkgo/README.md)
