**语言：** [English](../flash-guide.md) | 简体中文

# 刷机教程（Redmi Note 8 / ginkgo）

只适用于小米 **Redmi Note 8**（代号 **ginkgo**，SM6125）。不要刷到 willow、lavender 或其它机型。

需要 **解锁 bootloader**。刷 **userdata** 会清空 Android 和该分区上的全部数据。

## 下载什么

GitHub [Release](https://github.com/Huabin1010/ginkgo-mainline-linux/releases) 只放 **内核侧** 文件：

| 附件 | 刷到 | 说明 |
|------|------|------|
| `boot.img` | `boot` | 主线内核 + DTB + initramfs |
| `dtbo-empty.img` | `dtbo` | 24 MiB 全零，ABL 不叠加 overlay，改用 `boot.img` 里的 DTB |
| `SHA256SUMS` | — | 刷之前先校验 |

**故意不放进 Release：**

- `rootfs.ext4`：体积大，而且会把密码打进镜像。请在本机用 `scripts/build-rootfs.sh` 做。
- 原厂 / Lineage 的 `vbmeta.img`、`boot.img`：自己留备份，见 `backup/ginkgo/`。

手机里还是 Android、第一次装 Linux：先在电脑做好 Ubuntu 镜像并刷 userdata，再刷 Release 里的 `boot` + 空 `dtbo`。以后只更新内核时，**只刷 boot + dtbo**。

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

在电脑上做一次 rootfs：

```bash
git clone https://github.com/Huabin1010/ginkgo-mainline-linux.git
cd ginkgo-mainline-linux
./scripts/setup-deps.sh
# 镜像要用的密码（不入库）：
echo '你的密码' > rootfs-overlay/etc/ginkgo-root-password
chmod 600 rootfs-overlay/etc/ginkgo-root-password
./scripts/build-rootfs.sh
./scripts/configure-rootfs.sh
./scripts/build-rootfs-image.sh
```

下载 Release（示例标签 `v0.1.0`）：

```bash
gh release download v0.1.0 --repo Huabin1010/ginkgo-mainline-linux --dir out
# 或浏览器：https://github.com/Huabin1010/ginkgo-mainline-linux/releases
sha256sum -c out/SHA256SUMS
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

脚本会上传 `out/boot.img`、`out/dtbo-empty.img` 和 `SHA256SUMS`，并拒绝上传 `rootfs.ext4`。
