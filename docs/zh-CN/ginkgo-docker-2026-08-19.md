**语言：** [English](../ginkgo-docker-2026-08-19.md) | 简体中文

# Redmi Note 8 (ginkgo) 主线内核 Docker 适配与清华源安装

> 设备：Xiaomi Redmi Note 8 · codename **ginkgo** · SoC **SM6125 (trinket)** · 序列号 `<serial>`  
> 系统：主线 Linux 7.0 + Ubuntu 26.04 LTS（resolute）arm64，rootfs 在 userdata  
> 验收日：2026-08-19  
> 前置：USB RNDIS + SSH、WiFi、Ubuntu GNOME 桌面、Adreno 610、OSM CPUFreq 均已通。

**关联文档 / skill**

| 文档 | 内容 |
|------|------|
| [ginkgo-journey-to-remote-ssh.md](./ginkgo-journey-to-remote-ssh.md) | USB RNDIS 第一次打通 |
| [ginkgo-wifi-complete-2026-08-18.md](./ginkgo-wifi-complete-2026-08-18.md) | WCN3990，本次 apt / 清华源的前提 |
| [ginkgo-ubuntu-desktop-2026-08-19.md](./ginkgo-ubuntu-desktop-2026-08-19.md) | 清华 ubuntu-ports；本机 HTTPS CA 会失败 |
| [ginkgo-mainline-bringup-chronicle.md](./ginkgo-mainline-bringup-chronicle.md) | 全机时间线 |
| [usb-connect.sh](../scripts/usb-connect.sh) | 主机重配 RNDIS；现在会把口设成 unmanaged |
| [reboot-fastboot.sh](../scripts/reboot-fastboot.sh) | 从 Ubuntu 进 fastboot 刷 boot |

本次为了让新内核真正跑起来，写了 **boot + 空 dtbo**（`FLASH_ROOTFS=0`），**没有刷 userdata**。

---

## 0. 一句话结论

主线 ginkgo 内核原先 **namespace / cgroup v2 够用，Docker 网络和 overlay 不够用**：iptables/NAT 没编进去，`veth` / `bridge` / `overlay` 编成 module 但机上没有 `/lib/modules`。补成 built-in 并刷 boot 后，Moby 官方 `check-config.sh` **退出码从 1 变成 0**。随后用清华 `docker-ce` 源装上 Docker Engine **29.7.2**，`dockerd` 用 overlayfs + cgroup v2 + iptables 正常起来。

同一天还查清 USB 网「不稳定」不是 gadget 掉线，而是 **主机 NetworkManager 把 RNDIS 当普通网卡去 DHCP，失败后冲掉 `192.168.7.1`**；再叠加 gadget 每次随机 MAC。已改成固定 MAC + 主机 unmanaged。

---

## 1. 当天时间线

| 时间（CST） | 事件 |
|-------------|------|
| 上午 | 用户要求 SSH 上看 Docker 支持，跑官方测试脚本；机上当时无外网，主机用 `ghfast.top` 拉脚本再 scp |
| #137 内核 | `check-config.sh` 退出码 **1**；Docker / iptables / nft 都没装 |
| 随后 | 用户要求「完整适配 Docker」：改 `ginkgo.fragment`，编内核，`FLASH_ROOTFS=0` 刷 boot |
| 08:42 | 新内核编出（`#138`，`7.0.0-00001-g62f9dac57de4`） |
| 08:45 | `out/boot.img` 约 15 MiB |
| 08:46–08:47 | `reboot-fastboot` → 刷 dtbo + boot → 重启 |
| 08:47:00 | 机上 `usb-gadget-rndis` 绑定 UDC；08:47:21 sshd 起来 |
| 08:47:38 | **USB SSH 曾经成功过一次**（源地址 `192.168.7.1`） |
| 之后 | 主机 USB 口 IP 被 NetworkManager 冲掉；USB SSH 用 `-b 192.168.7.1` 报 `Cannot assign requested address` |
| 08:51 | 用户给出 WiFi `<wlan0-dhcp>`；改走 WiFi 复跑 `check-config.sh`，退出码 **0** |
| 08:53 | 查清 USB 不稳是主机 NM + 随机 MAC；固定 MAC，主机 unmanaged 后 USB SSH 1 秒通 |
| 08:54–08:59 | 清华源安装 Docker CE 29.7.2；`docker info` 正常 |

---

## 2. 第一次跑官方脚本（适配前，#137）

脚本是 Moby 的 [`contrib/check-config.sh`](https://github.com/moby/moby/blob/master/contrib/check-config.sh)。主机下载：

```text
https://ghfast.top/https://raw.githubusercontent.com/moby/moby/master/contrib/check-config.sh
```

scp 到手机 `/tmp/check-config.sh`，对 `/proc/config.gz` 执行。机上当时：

```text
Linux ginkgo 7.0.0-dirty #137 SMP PREEMPT ... aarch64
docker: command not found
/proc/config.gz          存在
/lib/modules/7.0.0-dirty 不存在
cgroup2 on /sys/fs/cgroup
iptables / nft / containerd 都没有
```

### 2.1 Generally Necessary：缺什么

**已经够用的：** namespaces（net/pid/ipc/uts）、cgroup 全家、KEYS、POSIX_MQUEUE、CGROUP_BPF；cgroup v2 控制器 `cpu / cpuset / io / memory / pids` 都在。

**配置里直接 missing（挡 bridge NAT）：**

- `CONFIG_IP_NF_FILTER` / `IP_NF_MANGLE` / `IP_NF_TARGET_MASQUERADE` / `IP_NF_RAW` / `IP_NF_NAT`
- 对应的 `CONFIG_IP6_NF_*`
- `CONFIG_NF_NAT`

**编成 `=m`、但机上等于没有**（`modprobe` 全部 FATAL）：

- `VETH`、`BRIDGE`、`BRIDGE_NETFILTER`、`OVERLAY_FS`
- `NETFILTER_XT_MATCH_{ADDRTYPE,CONNTRACK,IPVS}`、`XT_MARK`
- `IPV6`（所以 `/proc/sys/net/ipv6` 不存在）

`lsmod` 为空。这台机 **不部署 modules**，fragment 里 Docker 相关项必须全部 `=y`。

另：`net.ipv4.ip_forward=0`。

### 2.2 Optional：当时缺的

`BLK_DEV_THROTTLING`、`CFS_BANDWIDTH`、`NET_CLS_CGROUP`、`CGROUP_NET_PRIO`、`NF_TABLES` / `NFT_*`、`VXLAN`、`IPVLAN`、`DUMMY`、AppArmor、IPVS 细节、FTP/TFTP helper、加密 overlay 的 XFRM/ESP。存储侧 `OVERLAY_FS` 有配置但是 module。

ZFS / SELinux 一直没开（见 §7）。

---

## 3. Linux 7.0 里必须知道的 Kconfig 依赖

不能只写 `CONFIG_IP_NF_FILTER=y`。7.0 里这些表依赖 **legacy iptables**：

```text
IP_NF_FILTER / MANGLE / RAW / NAT  →  IP_NF_IPTABLES_LEGACY
IP_NF_IPTABLES_LEGACY              →  NETFILTER_XTABLES_LEGACY && !PREEMPT_RT
```

现代 Ubuntu 的 Docker 走 **iptables-nft**，所以同时还开了：

- `NF_TABLES` + `NF_TABLES_{INET,IPV4,IPV6,NETDEV}`
- `NFT_CT` / `NFT_MASQ` / `NFT_NAT` / `NFT_FIB*` / `NFT_COMPAT` / `NFT_REJECT` 等

`IPV6` 必须 `=y`（不能 `=m`），否则 `NF_TABLES_INET` 和 IPv6 forwarding sysctl 都起不来。

`SECURITY_APPARMOR=y` 只编译进去，**没有**改 `CONFIG_LSM`，也没有把 AppArmor 设成 default LSM（仍是 `DEFAULT_SECURITY_DAC`）。避免突然改强制访问控制把桌面搞挂。

SELinux 故意不开：Ubuntu 用 AppArmor。

---

## 4. 内核改了什么

全部写在 `config/ginkgo.fragment` 末尾，**一律 built-in**。`merge_config.sh` + `olddefconfig` 后抽查，下列符号全部是 `=y`，没有被丢掉。

### 4.1 Generally Necessary（脚本会红的那些）

IPv6、veth、bridge、`BRIDGE_NETFILTER`、conntrack、NAT、legacy iptables 全套、ip6tables 全套、xt_addrtype / xt_conntrack / xt_ipvs / xt_mark / MASQUERADE。

### 4.2 Optional（Docker Engine 会用到的）

| 类别 | 选项 |
|------|------|
| 存储 | `OVERLAY_FS`、`BTRFS_FS` + POSIX ACL |
| nftables | `NF_TABLES*`、`NFT_*` |
| cgroup 限制 | `BLK_DEV_THROTTLING`、`CFS_BANDWIDTH`、`NET_CLS_CGROUP`、`CGROUP_NET_PRIO` / `CLASSID` |
| 网络驱动 | `VXLAN`、`IPVLAN`、`MACVLAN`、`DUMMY`、`VLAN_8021Q`、`BRIDGE_VLAN_FILTERING` |
| IPVS | `IP_VS` + IPv6 + NFCT + TCP/UDP + RR |
| 加密 overlay | `CRYPTO_SEQIV`、`XFRM_USER`、`INET_ESP`、`XT_MATCH_BPF` |
| 其它 | `IP_SCTP`、`SECURITY_APPARMOR`、FTP/TFTP helper |

### 4.3 用户态转发

`rootfs-overlay/etc/sysctl.d/99-ginkgo-docker.conf`：

```text
net.ipv4.ip_forward = 1
net.ipv6.conf.all.forwarding = 1
net.ipv6.conf.default.forwarding = 1
```

当时没刷 rootfs，这份文件是 **WiFi scp 到活系统** 的。下次刷 userdata 会从 overlay 带上。

### 4.4 编译与刷入

```bash
./scripts/build-kernel.sh && ./scripts/build-bootimg.sh
# 机上：reboot-fastboot
FLASH_ROOTFS=0 ./scripts/flash-linux-boot.sh
```

产物：`out/boot.img`（刷入后 kernel_size 约 14.4 MiB）。新内核 `#138`。

刷完后 USB SSH 卡住时，用 WiFi：

```bash
ssh root@<wlan0-dhcp>    # 密码见 overlay；地址以当时 DHCP 为准
```

---

## 5. 适配后的 check-config.sh（#138，WiFi）

退出码 **0**。Generally Necessary **全部 enabled**（不再是 module）。

Optional 里脚本仍标 missing 的只有：

| 项 | 原因 |
|----|------|
| `CONFIG_SECURITY_SELINUX` | 故意不开，Ubuntu 走 AppArmor，内核已编 AppArmor |
| ZFS（`/dev/zfs`、`zfs`/`zpool`） | 树外模块；Docker 默认 overlay2，本机 overlay 已是 built-in |

`ip_forward` / IPv6 forwarding 均为 enabled。

---

## 6. USB 网络为什么看起来不稳定

### 6.1 当时主机侧现场

手机 `usb0`：**UP**，`192.168.7.2/24`，gadget 绑在 `4e00000.usb`，sshd 在 `0.0.0.0:22`。dmesg **没有** gadget disconnect。08:47:38 已经用 USB SSH 登录成功。

主机 `enx029eb9ff4712`（`rndis_host`）：

- 链路 UP，但 **没有 IPv4**
- `nmcli`：`ethernet:disconnected:`
- `ping -I 192.168.7.1`：`bind: 无法分配被请求的地址`

NetworkManager 日志（循环）：

```text
dhcp4 (enx029eb9ff4712): activation: beginning transaction (timeout in 45 seconds)
device ... state change: ip-config -> failed (reason 'ip-config-unavailable')
Activation: failed for connection '有线连接 2'
device ... state change: failed -> disconnected
```

手机 **没有 DHCP 服务器**。NM 把 RNDIS 当普通有线网卡，DHCP 失败后把接口打成 disconnected，并 **冲掉** `connect.sh` 刚加上的 `192.168.7.1`。于是 USB 上 ICMP/SSH 会「一会儿好一会儿 bind 失败」。这不是 dwc3 掉线。

仓库里已经堆了好几个废掉的 `ginkgo-usb` / `ginkgo-usb-tmp` 连接，都是历史 MAC 留下来的。

### 6.2 第二个坑：随机 MAC

`usb-gadget-rndis.sh` 以前每次 `start` 都 `RANDOM` 生成 `host_addr` / `dev_addr`。主机接口名每次变：

```text
enx02d706a7d0e6   →   enx029eb9ff4712
```

NM 每次都当新网卡，再开一轮 DHCP。文档和 skill 里「每次 reboot 必须重配 `enx*`」有一半就是这个原因。

### 6.3 修法

1. **固定 MAC**（gadget 脚本）：
   - 主机看到的：`02:00:00:00:07:01` → 接口名稳定为 **`enx020000000701`**
   - 手机 `usb0`：`02:00:00:00:07:02`
2. **主机不要托管这条口**：
   - `host/NetworkManager/99-ginkgo-rndis.conf` 装到 `/etc/NetworkManager/conf.d/`
   - `unmanaged-devices=driver:rndis_host;mac:02:00:00:00:07:01`
   - `connect.sh` / `host-usb-connect.sh` 配 IP 前先 `nmcli device set $iface managed no`
3. 机上脚本已用 WiFi 热更新并 `usb-gadget-rndis.sh restart`（没刷 rootfs）。overlay 里已是新脚本，下次刷 userdata 会带上。

复测：USB ping 1 秒通，`ssh -b 192.168.7.1 root@192.168.7.2` 1 秒登录。

刷机后那次后台 `connect.sh` 后来其实已经 SSH OK，接着退出码 127（`leep: 未找到命令`）：当时正在改同一个脚本，bash 边跑边读文件把 `sleep` 读坏了。现脚本是完整的。

---

## 7. 清华源安装 Docker

### 7.1 为什么用 HTTP

overlay 里 ubuntu-ports 已经写明：这张 rootfs **HTTPS 校验证书会失败**。清华 `docker-ce` 源同样走 HTTP：

```text
http://mirrors.tuna.tsinghua.edu.cn/docker-ce/linux/ubuntu
Suites: resolute
arch: arm64
```

清华 **有** `dists/resolute/`（2026-08-19），arm64 包齐全，不必降到 noble。

GPG：`http://mirrors.tuna.tsinghua.edu.cn/docker-ce/linux/ubuntu/gpg` → `/etc/apt/keyrings/docker.gpg`。

源文件：`/etc/apt/sources.list.d/docker.sources`（deb822），仓库 overlay 已同步。

清华这份是 **docker-ce 软件包镜像，不是 Docker Hub**。拉镜像还要另配 registry。

### 7.2 装了什么

```text
apt-get install docker-ce docker-ce-cli containerd.io \
  docker-buildx-plugin docker-compose-plugin
```

顺带拉上 `iptables` / `nftables`（引擎依赖）。

| 组件 | 版本 |
|------|------|
| Docker Engine | 29.7.2（`5:29.7.2-1~ubuntu.26.04~resolute`） |
| docker-ce-cli | 29.7.2 |
| containerd.io | 2.3.3 |
| runc | 1.4.3 |
| docker-buildx-plugin | 0.36.1 |
| docker-compose-plugin | 5.5.0 |
| iptables | 1.8.11（nft 后端） |

`ginkgo` 已加入 `docker` 组（该用户重新登录后生效）。root 可直接用。`docker.service` enabled + active。

### 7.3 `docker info` 验收

```text
Server Version: 29.7.2
Storage Driver: overlayfs
Cgroup Driver: systemd
Cgroup Version: 2
Network: bridge host ipvlan macvlan null overlay
Kernel Version: 7.0.0-00001-g62f9dac57de4
Operating System: Ubuntu 26.04 LTS
Architecture: aarch64
Firewall Backend: iptables
```

内核侧 Docker 功能与用户态引擎已经对上。当天 **没有** 再跑 `hello-world`（Hub 在国内常拉不下，清华也不镜像 Hub）。

---

## 8. 仓库里对应改动

| 路径 | 作用 |
|------|------|
| `config/ginkgo.fragment` | Docker / 容器所需内核选项，全部 `=y` |
| `rootfs-overlay/etc/sysctl.d/99-ginkgo-docker.conf` | 持久化 IPv4/IPv6 forwarding |
| `rootfs-overlay/etc/apt/sources.list.d/docker.sources` | 清华 docker-ce（HTTP、resolute） |
| `rootfs-overlay/usr/local/sbin/usb-gadget-rndis.sh` | 固定 RNDIS MAC |
| `scripts/usb-connect.sh` | 配 IP 前把 RNDIS 设成 unmanaged |
| `scripts/host-usb-connect.sh` | 同上 |
| `host/NetworkManager/99-ginkgo-rndis.conf` | 主机 NM drop-in（装到 `/etc/NetworkManager/conf.d/`） |

机上活系统额外有、overlay 下次刷 rootfs 才会对齐的：

- `/etc/apt/keyrings/docker.gpg`
- 已安装的 docker-ce 软件包
- `/etc/sysctl.d/99-ginkgo-docker.conf`（已 scp）
- 已热更新的 gadget 脚本

---

## 9. 常用命令

```bash
# 官方内核检查（脚本可从主机 ghfast.top 拉）
/tmp/check-config.sh /proc/config.gz

# USB SSH（固定口名之后）
./scripts/usb-connect.sh
ssh -b 192.168.7.1 root@192.168.7.2

# WiFi SSH（地址看 NetworkManager / ip -br addr）
ssh root@<wlan0-dhcp>

# Docker
docker version
docker info
```

刷只含 Docker 内核的 boot：

```bash
./scripts/build-kernel.sh && ./scripts/build-bootimg.sh
./scripts/reboot-fastboot.sh
FLASH_ROOTFS=0 ./scripts/flash-linux-boot.sh
```

---

## 10. 不要做

| 不要 | 原因 |
|------|------|
| 把 veth/bridge/overlay 留成 `=m` | 机上没有 `/lib/modules`，等于没开 |
| 只开 `IP_NF_FILTER` 不开 `XTABLES_LEGACY` | 7.0 里这些表依赖 legacy，`olddefconfig` 会丢掉 |
| 开 SELinux | 和 Ubuntu AppArmor 叠在一起没必要 |
| 把 AppArmor 设成 default LSM | 只编译即可；强行切换可能挡桌面 |
| 给 USB gadget 再改回随机 MAC | 主机口名又会变，NM 再 DHCP |
| 让主机 NM 托管 `enx*` / `rndis_host` | 会冲掉 `192.168.7.1` |
| 把 USB 口设成默认路由 | 会抢走家里网关，见 [chronicle §3.2](./ginkgo-mainline-bringup-chronicle.md) |
| 以为清华 docker-ce = 能拉 Hub 镜像 | 那是软件包仓库 |
| 对 tuna 改回 HTTPS | 这张 rootfs 的 CA 会校验失败 |
| 刷 userdata 只为装 Docker | 软件包已经装在现有 rootfs；overlay 只保证下次重做镜像还在 |
| `usbreset` 救 USB SSH | 见既有约定；先查主机有没有 `192.168.7.1` |

---

## 11. 和其它文档的边界

| 问题 | 文档 |
|------|------|
| USB RNDIS 第一次从无到有 | [journey-to-remote-ssh](./ginkgo-journey-to-remote-ssh.md) |
| 主机每次 reboot 重配 RNDIS | [usb-connect.sh](../scripts/usb-connect.sh) |
| WiFi 能上网 | [wifi 全记录](./ginkgo-wifi-complete-2026-08-18.md) |
| 清华 ubuntu-ports、HTTPS CA | [Ubuntu 桌面软件](./ginkgo-ubuntu-desktop-2026-08-19.md) |
| 内核 Docker 选项、check-config、引擎安装、USB NM | **本文** |

---

## 12. 还没做

- 没有配 Docker Hub / registry-mirrors，也没有跑 `hello-world`
- overlay 里的 docker.sources / sysctl / 固定 MAC 脚本，要等下次刷 userdata 才会和活系统「官方」对齐（活系统已经手工就位）
- 主机 NM drop-in 只装在当前这台 PC；换电脑要再拷 `host/NetworkManager/99-ginkgo-rndis.conf`
- CPU bwmon 仍 disabled（和 Docker 无关，见 [桌面性能](./ginkgo-desktop-perf-resources-2026-08-19.md)）
