**Language:** English | [简体中文](zh-CN/ginkgo-docker-2026-08-19.md)

# Redmi Note 8 (ginkgo) mainline kernel Docker bring-up and TUNA install

> Device: Xiaomi Redmi Note 8 · codename **ginkgo** · SoC **SM6125 (trinket)** · serial `<serial>`  
> System: mainline Linux 7.0 + Ubuntu 26.04 LTS (resolute) arm64, rootfs on userdata  
> Acceptance: 2026-08-19  
> Prerequisites: USB RNDIS + SSH, WiFi, Ubuntu GNOME desktop, Adreno 610, and OSM CPUFreq were already working.

**Related docs / skills**

| Document | Content |
|----------|---------|
| [ginkgo-journey-to-remote-ssh.md](./ginkgo-journey-to-remote-ssh.md) | USB RNDIS first working |
| [ginkgo-wifi-complete-2026-08-18.md](./ginkgo-wifi-complete-2026-08-18.md) | WCN3990; prerequisite for this apt / TUNA run |
| [ginkgo-ubuntu-desktop-2026-08-19.md](./ginkgo-ubuntu-desktop-2026-08-19.md) | TUNA ubuntu-ports; HTTPS CA fails on this device |
| [ginkgo-mainline-bringup-chronicle.md](./ginkgo-mainline-bringup-chronicle.md) | Full-device timeline |
| [usb-connect.sh](../scripts/usb-connect.sh) | Host reconfigures RNDIS; now also marks the interface unmanaged |
| [reboot-fastboot.sh](../scripts/reboot-fastboot.sh) | Enter fastboot from Ubuntu and flash boot |

This time, to make the new kernel actually run, **boot + empty dtbo** were written (`FLASH_ROOTFS=0`). **userdata was not flashed.**

---

## 0. One-sentence conclusion

The mainline ginkgo kernel already had **enough namespace / cgroup v2, but not enough Docker networking and overlay**: iptables/NAT were not built in; `veth` / `bridge` / `overlay` were modules, but the device has no `/lib/modules`. After they were built-in and boot was flashed, Moby’s official `check-config.sh` **exit code went from 1 to 0**. Docker Engine **29.7.2** was then installed from the TUNA `docker-ce` archive; `dockerd` came up normally with overlayfs + cgroup v2 + iptables.

The same day also showed that “unstable” USB networking was not the gadget dropping, but **host NetworkManager treating RNDIS as a normal NIC, DHCP-failing, then wiping `192.168.7.1`**, plus a random MAC on every gadget start. Fixed MAC + host unmanaged.

---

## 1. Timeline that day

| Time (CST) | Event |
|------------|-------|
| Morning | User asked to check Docker support over SSH and run the official test script; the phone had no WAN then; host fetched the script via `ghfast.top` and scp’d it |
| Kernel #137 | `check-config.sh` exit code **1**; Docker / iptables / nft not installed |
| After that | User asked for “full Docker support”: change `ginkgo.fragment`, build the kernel, flash boot with `FLASH_ROOTFS=0` |
| 08:42 | New kernel built (`#138`, `7.0.0-00001-g62f9dac57de4`) |
| 08:45 | `out/boot.img` ~15 MiB |
| 08:46–08:47 | `reboot-fastboot` → flash dtbo + boot → reboot |
| 08:47:00 | On-device `usb-gadget-rndis` bound the UDC; 08:47:21 sshd came up |
| 08:47:38 | **USB SSH succeeded once** (source `192.168.7.1`) |
| After that | Host USB interface IP was wiped by NetworkManager; USB SSH with `-b 192.168.7.1` reported `Cannot assign requested address` |
| 08:51 | User gave WiFi `<wlan0-dhcp>`; switched to WiFi and reran `check-config.sh`, exit code **0** |
| 08:53 | USB instability traced to host NM + random MAC; after fixed MAC and host unmanaged, USB SSH worked in 1 second |
| 08:54–08:59 | Installed Docker CE 29.7.2 from TUNA; `docker info` looked correct |

---

## 2. First official-script run (before bring-up, #137)

The script is Moby’s [`contrib/check-config.sh`](https://github.com/moby/moby/blob/master/contrib/check-config.sh). Host download:

```text
https://ghfast.top/https://raw.githubusercontent.com/moby/moby/master/contrib/check-config.sh
```

scp to the phone `/tmp/check-config.sh`, run against `/proc/config.gz`. On the device then:

```text
Linux ginkgo 7.0.0-dirty #137 SMP PREEMPT ... aarch64
docker: command not found
/proc/config.gz          present
/lib/modules/7.0.0-dirty missing
cgroup2 on /sys/fs/cgroup
iptables / nft / containerd all missing
```

### 2.1 Generally Necessary: what was missing

**Already enough:** namespaces (net/pid/ipc/uts), full cgroup set, KEYS, POSIX_MQUEUE, CGROUP_BPF; cgroup v2 controllers `cpu / cpuset / io / memory / pids` all present.

**Directly missing in config (blocks bridge NAT):**

- `CONFIG_IP_NF_FILTER` / `IP_NF_MANGLE` / `IP_NF_TARGET_MASQUERADE` / `IP_NF_RAW` / `IP_NF_NAT`
- Matching `CONFIG_IP6_NF_*`
- `CONFIG_NF_NAT`

**Built as `=m`, which on this device equals missing** (`modprobe` all FATAL):

- `VETH`, `BRIDGE`, `BRIDGE_NETFILTER`, `OVERLAY_FS`
- `NETFILTER_XT_MATCH_{ADDRTYPE,CONNTRACK,IPVS}`, `XT_MARK`
- `IPV6` (so `/proc/sys/net/ipv6` does not exist)

`lsmod` was empty. This device **does not deploy modules**; every Docker-related item in the fragment must be `=y`.

Also: `net.ipv4.ip_forward=0`.

### 2.2 Optional: missing at the time

`BLK_DEV_THROTTLING`, `CFS_BANDWIDTH`, `NET_CLS_CGROUP`, `CGROUP_NET_PRIO`, `NF_TABLES` / `NFT_*`, `VXLAN`, `IPVLAN`, `DUMMY`, AppArmor, IPVS details, FTP/TFTP helpers, XFRM/ESP for encrypted overlay. Storage-side `OVERLAY_FS` was configured but as a module.

ZFS / SELinux have stayed off (see §7).

---

## 3. Kconfig dependencies you must know on Linux 7.0

You cannot only write `CONFIG_IP_NF_FILTER=y`. On 7.0 these tables depend on **legacy iptables**:

```text
IP_NF_FILTER / MANGLE / RAW / NAT  →  IP_NF_IPTABLES_LEGACY
IP_NF_IPTABLES_LEGACY              →  NETFILTER_XTABLES_LEGACY && !PREEMPT_RT
```

Modern Ubuntu Docker uses **iptables-nft**, so these were also enabled:

- `NF_TABLES` + `NF_TABLES_{INET,IPV4,IPV6,NETDEV}`
- `NFT_CT` / `NFT_MASQ` / `NFT_NAT` / `NFT_FIB*` / `NFT_COMPAT` / `NFT_REJECT` and related

`IPV6` must be `=y` (not `=m`), or `NF_TABLES_INET` and the IPv6 forwarding sysctls never come up.

`SECURITY_APPARMOR=y` is compiled in only. **`CONFIG_LSM` was not changed**, and AppArmor was not made the default LSM (still `DEFAULT_SECURITY_DAC`). Avoid suddenly flipping mandatory access control and hanging the desktop.

SELinux is deliberately off: Ubuntu uses AppArmor.

---

## 4. What changed in the kernel

All of it is at the end of `config/ginkgo.fragment`, **all built-in**. After `merge_config.sh` + `olddefconfig`, spot-checks showed every symbol below is `=y`; none were dropped.

### 4.1 Generally Necessary (the ones the script marks red)

IPv6, veth, bridge, `BRIDGE_NETFILTER`, conntrack, NAT, full legacy iptables, full ip6tables, xt_addrtype / xt_conntrack / xt_ipvs / xt_mark / MASQUERADE.

### 4.2 Optional (what Docker Engine uses)

| Category | Options |
|----------|---------|
| Storage | `OVERLAY_FS`, `BTRFS_FS` + POSIX ACL |
| nftables | `NF_TABLES*`, `NFT_*` |
| cgroup limits | `BLK_DEV_THROTTLING`, `CFS_BANDWIDTH`, `NET_CLS_CGROUP`, `CGROUP_NET_PRIO` / `CLASSID` |
| Net drivers | `VXLAN`, `IPVLAN`, `MACVLAN`, `DUMMY`, `VLAN_8021Q`, `BRIDGE_VLAN_FILTERING` |
| IPVS | `IP_VS` + IPv6 + NFCT + TCP/UDP + RR |
| Encrypted overlay | `CRYPTO_SEQIV`, `XFRM_USER`, `INET_ESP`, `XT_MATCH_BPF` |
| Other | `IP_SCTP`, `SECURITY_APPARMOR`, FTP/TFTP helpers |

### 4.3 Userspace forwarding

`rootfs-overlay/etc/sysctl.d/99-ginkgo-docker.conf`:

```text
net.ipv4.ip_forward = 1
net.ipv6.conf.all.forwarding = 1
net.ipv6.conf.default.forwarding = 1
```

rootfs was not flashed that time; this file was **scp’d over WiFi onto the live system**. The next userdata flash will bring it from the overlay.

### 4.4 Build and flash

```bash
./scripts/build-kernel.sh && ./scripts/build-bootimg.sh
# on device: reboot-fastboot
FLASH_ROOTFS=0 ./scripts/flash-linux-boot.sh
```

Artifact: `out/boot.img` (after flash, kernel_size ~14.4 MiB). New kernel `#138`.

When USB SSH stuck after flash, use WiFi:

```bash
ssh root@<wlan0-dhcp>    # password: see overlay; address is whatever DHCP gave at the time
```

---

## 5. check-config.sh after bring-up (#138, WiFi)

Exit code **0**. Generally Necessary **all enabled** (no longer modules).

The only Optional items the script still marks missing:

| Item | Why |
|------|-----|
| `CONFIG_SECURITY_SELINUX` | Deliberately off; Ubuntu uses AppArmor; AppArmor is already built in the kernel |
| ZFS (`/dev/zfs`, `zfs`/`zpool`) | Out-of-tree module; Docker defaults to overlay2; overlay is already built-in here |

`ip_forward` / IPv6 forwarding both enabled.

---

## 6. Why USB networking looked unstable

### 6.1 Host-side scene at the time

Phone `usb0`: **UP**, `192.168.7.2/24`, gadget bound to `4e00000.usb`, sshd on `0.0.0.0:22`. dmesg had **no** gadget disconnect. USB SSH had already logged in successfully at 08:47:38.

Host `enx029eb9ff4712` (`rndis_host`):

- Link UP, but **no IPv4**
- `nmcli`: `ethernet:disconnected:`
- `ping -I 192.168.7.1`: `bind: 无法分配被请求的地址`

NetworkManager log (looping):

```text
dhcp4 (enx029eb9ff4712): activation: beginning transaction (timeout in 45 seconds)
device ... state change: ip-config -> failed (reason 'ip-config-unavailable')
Activation: failed for connection '有线连接 2'
device ... state change: failed -> disconnected
```

The phone **has no DHCP server**. NM treated RNDIS as a normal wired NIC, failed DHCP, marked the interface disconnected, and **wiped** the `192.168.7.1` that `connect.sh` had just added. ICMP/SSH on USB then looked “sometimes fine, sometimes bind failed.” This is not dwc3 dropping.

The repo already has several dead `ginkgo-usb` / `ginkgo-usb-tmp` connections left over from historical MACs.

### 6.2 Second pitfall: random MAC

`usb-gadget-rndis.sh` used to `RANDOM` generate `host_addr` / `dev_addr` on every `start`. The host interface name changed each time:

```text
enx02d706a7d0e6   →   enx029eb9ff4712
```

NM treated each as a new NIC and started another DHCP round. Half of the “must reconfigure `enx*` after every reboot” in the docs and skill is this.

### 6.3 Fix

1. **Fixed MAC** (gadget script):
   - What the host sees: `02:00:00:00:07:01` → interface name stable as **`enx020000000701`**
   - Phone `usb0`: `02:00:00:00:07:02`
2. **Do not let the host manage this interface**:
   - `host/NetworkManager/99-ginkgo-rndis.conf` installed to `/etc/NetworkManager/conf.d/`
   - `unmanaged-devices=driver:rndis_host;mac:02:00:00:00:07:01`
   - `connect.sh` / `host-usb-connect.sh` run `nmcli device set $iface managed no` before setting the IP
3. The on-device script was hot-updated over WiFi and `usb-gadget-rndis.sh restart` (rootfs not flashed). The overlay already has the new script; the next userdata flash will include it.

Retest: USB ping in 1 second, `ssh -b 192.168.7.1 root@192.168.7.2` login in 1 second.

The background `connect.sh` after that flash later actually reached SSH OK, then exit 127 (`leep: 未找到命令`): the same script was being edited while bash was reading it mid-run and broke `sleep`. The current script is complete.

---

## 7. Installing Docker from TUNA

### 7.1 Why HTTP

The overlay ubuntu-ports already states: this rootfs **fails HTTPS certificate verification**. The TUNA `docker-ce` archive is HTTP as well:

```text
http://mirrors.tuna.tsinghua.edu.cn/docker-ce/linux/ubuntu
Suites: resolute
arch: arm64
```

TUNA **has** `dists/resolute/` (2026-08-19), with a full arm64 package set; no need to drop to noble.

GPG: `http://mirrors.tuna.tsinghua.edu.cn/docker-ce/linux/ubuntu/gpg` → `/etc/apt/keyrings/docker.gpg`.

Source file: `/etc/apt/sources.list.d/docker.sources` (deb822); repo overlay is already synced.

This TUNA tree is a **docker-ce package mirror, not Docker Hub**. Pulling images still needs a separate registry config.

### 7.2 What was installed

```text
apt-get install docker-ce docker-ce-cli containerd.io \
  docker-buildx-plugin docker-compose-plugin
```

That also pulled `iptables` / `nftables` (engine dependencies).

| Component | Version |
|-----------|---------|
| Docker Engine | 29.7.2 (`5:29.7.2-1~ubuntu.26.04~resolute`) |
| docker-ce-cli | 29.7.2 |
| containerd.io | 2.3.3 |
| runc | 1.4.3 |
| docker-buildx-plugin | 0.36.1 |
| docker-compose-plugin | 5.5.0 |
| iptables | 1.8.11 (nft backend) |

`ginkgo` was added to the `docker` group (takes effect after that user logs in again). root can use it immediately. `docker.service` enabled + active.

### 7.3 `docker info` acceptance

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

Kernel Docker features and the userspace engine now match. **`hello-world` was not run** that day (Hub is often unreachable in China; TUNA does not mirror Hub either).

---

## 8. Matching repo changes

| Path | Purpose |
|------|---------|
| `config/ginkgo.fragment` | Kernel options Docker / containers need, all `=y` |
| `rootfs-overlay/etc/sysctl.d/99-ginkgo-docker.conf` | Persistent IPv4/IPv6 forwarding |
| `rootfs-overlay/etc/apt/sources.list.d/docker.sources` | TUNA docker-ce (HTTP, resolute) |
| `rootfs-overlay/usr/local/sbin/usb-gadget-rndis.sh` | Fixed RNDIS MAC |
| `scripts/usb-connect.sh` | Mark RNDIS unmanaged before setting the IP |
| `scripts/host-usb-connect.sh` | Same |
| `host/NetworkManager/99-ginkgo-rndis.conf` | Host NM drop-in (install to `/etc/NetworkManager/conf.d/`) |

On the live system extra, aligned with overlay only on the next rootfs flash:

- `/etc/apt/keyrings/docker.gpg`
- Installed docker-ce packages
- `/etc/sysctl.d/99-ginkgo-docker.conf` (already scp’d)
- Hot-updated gadget script

---

## 9. Common commands

```bash
# Official kernel check (script can be fetched on the host via ghfast.top)
/tmp/check-config.sh /proc/config.gz

# USB SSH (after the fixed interface name)
./scripts/usb-connect.sh
ssh -b 192.168.7.1 root@192.168.7.2

# WiFi SSH (address from NetworkManager / ip -br addr)
ssh root@<wlan0-dhcp>

# Docker
docker version
docker info
```

Flash a boot that only adds the Docker kernel:

```bash
./scripts/build-kernel.sh && ./scripts/build-bootimg.sh
./scripts/reboot-fastboot.sh
FLASH_ROOTFS=0 ./scripts/flash-linux-boot.sh
```

---

## 10. Do not

| Do not | Why |
|--------|-----|
| Leave veth/bridge/overlay as `=m` | No `/lib/modules` on the device; same as off |
| Enable `IP_NF_FILTER` without `XTABLES_LEGACY` | On 7.0 these tables depend on legacy; `olddefconfig` will drop them |
| Enable SELinux | No need to stack it on Ubuntu AppArmor |
| Make AppArmor the default LSM | Compile it in only; forcing the switch may block the desktop |
| Change the USB gadget back to a random MAC | Host interface name changes again; NM DHCP again |
| Let host NM manage `enx*` / `rndis_host` | Wipes `192.168.7.1` |
| Make the USB interface the default route | Steals the home gateway; see [chronicle §3.2](./ginkgo-mainline-bringup-chronicle.md) |
| Treat TUNA docker-ce as “can pull Hub images” | That is a package archive |
| Switch tuna back to HTTPS | CA verification fails on this rootfs |
| Flash userdata only to install Docker | Packages are already on the current rootfs; overlay only guarantees they survive the next image rebuild |
| `usbreset` to “save” USB SSH | See existing convention; first check whether the host still has `192.168.7.1` |

---

## 11. Boundaries vs other docs

| Topic | Document |
|-------|----------|
| USB RNDIS from nothing to working | [journey-to-remote-ssh](./ginkgo-journey-to-remote-ssh.md) |
| Host reconfigures RNDIS after every reboot | [usb-connect.sh](../scripts/usb-connect.sh) |
| WiFi can reach the internet | [wifi complete record](./ginkgo-wifi-complete-2026-08-18.md) |
| TUNA ubuntu-ports, HTTPS CA | [Ubuntu desktop software](./ginkgo-ubuntu-desktop-2026-08-19.md) |
| Kernel Docker options, check-config, engine install, USB NM | **This document** |

---

## 12. Not done yet

- No Docker Hub / registry-mirrors configured, and `hello-world` was not run
- Overlay docker.sources / sysctl / fixed-MAC script only “officially” match the live system on the next userdata flash (the live system is already in place by hand)
- The host NM drop-in is installed only on this PC; another machine needs another copy of `host/NetworkManager/99-ginkgo-rndis.conf`
- CPU bwmon is still disabled (unrelated to Docker; see [desktop performance](./ginkgo-desktop-perf-resources-2026-08-19.md))
