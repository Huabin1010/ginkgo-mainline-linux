**语言：** [English](../flash-guide.md) | 简体中文

# 刷机教程（Redmi Note 8 / ginkgo）

只适用于小米 **Redmi Note 8**（代号 **ginkgo**，SM6125）。不要刷到 willow、lavender 或其它机型。

需要 **解锁 bootloader**。刷 **userdata** 会清空 Android 和该分区上的全部数据。

## 下载什么

GitHub [Release](https://github.com/Huabin1010/ginkgo-mainline-linux/releases) 提供可刷的镜像：

| 附件 | 刷到 | 说明 |
|------|------|------|
| `boot.img` | `boot` | 主线内核 + DTB + initramfs（**天马 NT36672A**） |
| `boot-huaxing.img` | `boot`（测试） | 实验性 **华星 FT8719** 面板。可能黑屏，触控关闭。建议先 `fastboot boot` 试一次再 flash |
| `dtbo-empty.img` | `dtbo` | 24 MiB 全零，ABL 不叠加 overlay，改用 `boot.img` 里的 DTB |
| `rootfs.ext4.zst` | 解压后刷 `userdata` | 原始 `rootfs.ext4` 刚好 2 GiB，GitHub 拒收；先解压。**会清空 userdata。** |
| `SHA256SUMS` | — | 刷之前先校验 |

默认镜像只适配天马。华星测试见主 README 的 [面板批次](../../README.zh-CN.md#面板批次天马--华星)（如何区分、串口/USB 日志、提 Issue）。

Release 里的 rootfs 是**早期最小镜像**，不是后来的完整 GNOME 桌面。第一次开机后执行 `passwd`。也可以继续用 `scripts/build-rootfs.sh` 在本机做更新的镜像。

原厂 / Lineage 的 `vbmeta.img`、`boot.img` 不在 Release 里，自己留备份，见 `backup/ginkgo/`。

第一次装：解压 rootfs，刷 userdata，再刷 `boot` + 空 `dtbo`。以后只更新内核时，**只刷 boot + dtbo**。

## 电脑工具

```bash
sudo apt install android-sdk-platform-tools   # adb, fastboot
# 或：export PATH="$HOME/.local/bin:$PATH"
```

确认解锁：

```bash
fastboot devices
fastboot getvar unlocked          # 应为 yes
fastboot getvar product           # 必须是 ginkgo
```

`product` 不是 `ginkgo` 就停手。

## 首次安装（会清空 userdata）

下载 Release（示例标签 `v0.1.0`）：

```bash
gh release download v0.1.0 --repo Huabin1010/ginkgo-mainline-linux --dir out
# 或浏览器：https://github.com/Huabin1010/ginkgo-mainline-linux/releases
cd out && sha256sum -c SHA256SUMS
sudo apt install zstd
zstd -d -f rootfs.ext4.zst          # → rootfs.ext4（2 GiB）
```

也可以在电脑上自己做更新 / 完整桌面的 rootfs：

```bash
git clone https://github.com/Huabin1010/ginkgo-mainline-linux.git
cd ginkgo-mainline-linux
./scripts/setup-deps.sh
echo '你的密码' > rootfs-overlay/etc/ginkgo-root-password
chmod 600 rootfs-overlay/etc/ginkgo-root-password
./scripts/build-rootfs.sh
./scripts/configure-rootfs.sh
./scripts/build-rootfs-image.sh
```

进 fastboot（`adb reboot bootloader`，或音量− + 电源），然后：

```bash
export PATH="$HOME/.local/bin:$PATH"

# 1. Ubuntu → userdata（清空该分区）
fastboot flash userdata out/rootfs.ext4

# 2. 空 DTBO，避免 ABL overlay 盖掉主线 DTB
fastboot flash dtbo out/dtbo-empty.img

# 3. 主线内核
fastboot flash boot out/boot.img

# 4. 关掉 verified boot（用你备份的 vbmeta，或任意合法的 vbmeta.img）
fastboot flash vbmeta --disable-verification backup/ginkgo/vbmeta.img

fastboot reboot
```

`backup/ginkgo/vbmeta.img` **不入库**。没有的话，ginkgo 的原厂 / Lineage `vbmeta.img` 加上 `--disable-verification` 即可。

## 只更新内核（保留 Ubuntu）

rootfs 已经在跑，只换新内核：

```bash
fastboot flash dtbo dtbo-empty.img
fastboot flash boot boot.img
fastboot reboot
```

除非你要清空系统，否则不要再刷 userdata。

在 Ubuntu 里可以用 `reboot-fastboot`（或 `reboot bootloader`）进 fastboot，再在电脑上刷。

## 开机之后

USB RNDIS：手机 `192.168.7.2`，电脑 `192.168.7.1`。

```bash
./scripts/usb-connect.sh
# 或：ssh -b 192.168.7.1 root@192.168.7.2
```

串口（可选，**只用 1.8V**）：`ttyMSM0,115200n8`。接线见 [UART 指南](ginkgo-usb-ttl-uart.md)。

## 恢复 Android

需要刷 Linux **之前** 备份的 Lineage / 原厂 `boot.img`、`dtbo.img`、`vbmeta.img`。这些文件不入库。

```bash
./scripts/restore-android.sh
```

若 userdata 已经是 Ubuntu，在 TWRP 里 wipe，或 `fastboot -w`（会清空数据），再刷回 ROM。

## 不要做

- 把这些镜像刷到非 ginkgo 设备
- 把 `rootfs.ext4` 或密码放进公开 Release
- 调试串口用 3.3V / 5V
- 没放行 22 就打开手机 ufw
- 除非要写分区，否则不要 `fastboot flash boot`（只想试一次用 `fastboot boot boot.img`）

## 发布 Release（维护者）

接口：GitHub Releases REST API，用 `gh` 调用。

```bash
gh auth login          # 只需一次；git 的 SSH 不能代替 API 登录
./scripts/publish-release.sh v0.1.0
```

脚本会上传 `out/boot.img`、`out/dtbo-empty.img` 和 `SHA256SUMS`。若存在 `out/boot-huaxing.img` 也会一并上传。若存在 `out/rootfs.ext4`，会先压成 `rootfs.ext4.zst`（GitHub 拒收刚好 2 GiB 的原文件）再上传。
