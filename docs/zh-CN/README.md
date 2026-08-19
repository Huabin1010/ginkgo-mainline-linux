# 文档目录

**语言：** [English](../README.md) | 简体中文

默认文档是英文，在 [`../`](../README.md)。本目录是中文原稿。每篇页首都有语言切换。

## 从这里开始

| 文档 | 内容 |
|------|------|
| [flash-guide.md](flash-guide.md) | 刷 GitHub Release（`boot.img` + 空 dtbo） |
| [ginkgo-mainline-bringup-chronicle.md](ginkgo-mainline-bringup-chronicle.md) | 全机 bring-up 时间线与当前状态 |
| [mainline-ginkgo-porting-guide.md](mainline-ginkgo-porting-guide.md) | 硬件清单、与主线差距、分阶段计划 |
| [ginkgo-usb-ttl-uart.md](ginkgo-usb-ttl-uart.md) | 1.8V UART 接线（不要焊 EDL 点） |
| [uart-debug-ginkgo.md](uart-debug-ginkgo.md) | UART 速查 |

## 里程碑

| 日期 | 文档 |
|------|------|
| 2026-08-17 | [显示：品红 framebuffer 上屏](ginkgo-display-complete-2026-08-17.md) |
| 2026-08-17 | [触控：NT36672A SPI](ginkgo-touch-complete-2026-08-17.md) |
| 2026-08-18 | [fbcon 启动日志上屏](ginkgo-fbcon-boot-2026-08-18.md) |
| 2026-08-18 | [Wi-Fi：WCN3990 / ath10k_snoc](ginkgo-wifi-complete-2026-08-18.md) |
| 2026-08-19 | [Ubuntu GNOME 桌面](ginkgo-ubuntu-desktop-2026-08-19.md) |
| 2026-08-19 | [Adreno 610 GPU](ginkgo-gpu-desktop-2026-08-19.md) |
| 2026-08-19 | [桌面流畅度 / Resources](ginkgo-desktop-perf-resources-2026-08-19.md) |
| 2026-08-19 | [Docker CE](ginkgo-docker-2026-08-19.md) |

## 历史与方法

| 文档 | 内容 |
|------|------|
| [ginkgo-bringup-journal.md](ginkgo-bringup-journal.md) | 早期启动 / init 日志（截至 2026-08-08，已冻结） |
| [ginkgo-journey-to-remote-ssh.md](ginkgo-journey-to-remote-ssh.md) | USB RNDIS + SSH |
| [mainline-boot-failure-analysis.md](mainline-boot-failure-analysis.md) | 最初「刷完回 fastboot」 |
| [ginkgo-display-bringup-methodology.md](ginkgo-display-bringup-methodology.md) | LCDB / DCS / PHY / DPMS 方法 |
| [ginkgo-dsi-err-status5-analysis.md](ginkgo-dsi-err-status5-analysis.md) | 历史 `dsi_err status=5` |
| [postmarketOS-ginkgo-research.md](postmarketOS-ginkgo-research.md) | postmarketOS 调研 |
| [ginkgo-phone-web-2026-08-19.md](ginkgo-phone-web-2026-08-19.md) | 公网 HTTPS 透传到手机（已脱敏） |
| [thinking.md](thinking.md) | 显示寄存器原始笔记（未翻译） |

## 其它 README

- [firmware/ginkgo](../../firmware/ginkgo/README.zh-CN.md)
- [reference](../../reference/README.zh-CN.md)
- [backup/ginkgo](../../backup/ginkgo/README.zh-CN.md)
