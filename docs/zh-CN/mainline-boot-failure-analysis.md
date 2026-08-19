**语言：** [English](../mainline-boot-failure-analysis.md) | 简体中文

# 主线内核启动失败分析（2026-08-05）

设备：ginkgo `<serial>`，现象：刷入 `out/boot.img` 后约数秒回到 fastboot，无 adb。

## 结论（按可能性）

| # | 原因 | 证据 | 严重度 | 状态 |
|---|------|------|--------|------|
| 0 | **`CONFIG_SM_GCC_6125` 未启用** | arm64 defconfig **没有**开 SM6125 GCC；无全局时钟则 UART/MMC/总线无法工作 | **致命** | 修复中 |
| 1 | **`CONFIG_ARM64_VA_BITS_52=y` 不适合 SM6125** | Kryo 260 无 FEAT_LVA | **高** | 已改为 39 |
| 2 | **`qcom,board-id` 与本机不匹配** | 本机/`DTBO`=`0x22`；主线曾为 `0x16` | **高** | 已改为 `0x22` |
| 3 | **原厂 DTBO 叠到主线 DTB** | 空 DTBO 已刷入测试 | 中 | 已处理 |
| 4 | **simplefb 未开** | DTS 用 `simple-framebuffer`，但 `FB_SIMPLE`/`SYSFB_SIMPLEFB` 未开 | 中 | 修复中 |
| 5 | pstore 地址不一致 | 主线 `@0xffc40000` vs TWRP `@0x61600000` | 中（排障） | 待修 |
| 6 | 无串口 | 无 UART 线 | 中 | — |

## 实测对比

### board-id / msm-id

| 来源 | msm-id | board-id |
|------|--------|----------|
| 本机运行中 DTS | `0x18a / 0x10000` | **`0x22 / 0x00`** |
| 主线 `sm6125-xiaomi-ginkgo.dtb` | `0x18a / 0x10000` | **`0x16 / 0x00`（错误）** |
| 原厂 DTBO | `0x18a / 0x10000` | **`0x22 / 0x00`（匹配）** |

ABL 按 msm-id + board-id 选 DTB；不匹配时常直接回 fastboot。

### boot.img 格式（已修）

第二次打包已改为 header v2 + 独立 DTB，与原厂一致；仍失败 → 不是打包格式 alone。

### 内核配置嫌疑

```
CONFIG_ARM64_VA_BITS_52=y   # 当前 out/kernel/.config — 应改为 39
```

建议 fragment：

```
# CONFIG_ARM64_VA_BITS_52 is not set
CONFIG_ARM64_VA_BITS_39=y
CONFIG_ARM64_VA_BITS=39
```

## 修复步骤（下一步）

1. 修正 `config/ginkgo.fragment`：强制 `VA_BITS=39`
2. 修正 DTB：`qcom,board-id = <0x22 0>`（或同时提供 0x16/0x22）
3. 统一 ramoops 地址（与下游 `0x61600000` 对齐，便于 recovery 抓 log）
4. 重新 `build-kernel.sh` + `build-bootimg.sh`
5. 在 TWRP 用 `dd` 刷 boot（避免 fastboot USB 卡顿），或 `fastboot boot` 试启动
6. 失败则进 recovery 跑 `./scripts/capture-logs.sh`（修好 pstore 地址后）

## 为何 recovery 抓不到主线日志

主线 DTS：

```
ramoops@ffc00000 → reg = <0xffc40000 0xc0000>
```

TWRP 内核：

```
ramoops: attached 0x400000@0x61600000
```

地址不同 → `/sys/fs/pstore` 为空属预期。

## 复测记录（2026-08-05 01:00–01:16）

| 尝试 | 结果 |
|------|------|
| VA_BITS=39 + board-id=0x22 | 仍回 fastboot |
| + 空 DTBO + Ubuntu rootfs | 仍回 fastboot；且 `fastboot reboot recovery` 失败 |
| + `CONFIG_SM_GCC_6125` + simplefb | 仍回 fastboot |
| 原厂 DTBO + GCC6125 boot | 仍回 fastboot |
| 恢复原厂 boot+dtbo | recovery 正常 |

**结论：** 配置层已知问题已修，仍无法越过 ABL/极早期内核。下一步必须 **UART 抓 earlycon**（见 `docs/uart-debug-ginkgo.md`）。
