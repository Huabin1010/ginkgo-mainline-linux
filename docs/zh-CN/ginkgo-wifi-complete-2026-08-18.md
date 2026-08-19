**语言：** [English](../ginkgo-wifi-complete-2026-08-18.md) | 简体中文

# Redmi Note 8 (ginkgo) 主线 WiFi：从「没有 wlan0」到 5 GHz VHT80 关联并显示速率

> 设备：Xiaomi Redmi Note 8 · codename **ginkgo** · SoC **SM6125 (trinket)** · 序列号 `<serial>`  
> 芯片：Qualcomm **WCN3990** · 总线 **SNOC** `wifi@c800000` · 驱动 **`ath10k_snoc`**（不是 `wcn36xx`，不是下游 `icnss`）  
> 固件：**本机** `wlanmdsp.mbn` = `WLAN.HL.3.0.2-00656`（**不要**混用 linux-firmware 的 HL.2.0）  
> BDF：`bdf_c3j.bin` 原样当 `board.bin`（26328 B），**不是** `board-2.bin` IE 包装  
> 天线：固件 `num_rf_chains=1`（1x1）；HT MCS 0–7；VHT NSS1 MCS 0–9  
> QMI：`chip_id 0x120`，`board_id 0xff`  
> 验收日：2026-08-18 · 关联 `<test-ap-5g>`（ch36 / 5180 / VHT80），IP `<wlan0-dhcp>`，速率 **292.5 MBit/s VHT-MCS 7 80MHz**  
> 前置：同机显示 P3、触控 P4 已通。不要为修 WiFi 去改 pinmux / CS / `CONFIG_FB` / 显示预取。

**关联文档 / skill**

| 文档 | 内容 |
|------|------|
| [ginkgo-display-complete-2026-08-17.md](./ginkgo-display-complete-2026-08-17.md) | 显示出图（本机前置） |
| [ginkgo-touch-complete-2026-08-17.md](./ginkgo-touch-complete-2026-08-17.md) | 触控出点（本机前置） |
| [ginkgo-mainline-bringup-chronicle.md](./ginkgo-mainline-bringup-chronicle.md) | 全机 bring-up 时间线 |
| [firmware/ginkgo/README.md](../firmware/ginkgo/README.md) | 本机提取的 wlanmdsp / BDF |
| [usb-connect.sh](../scripts/usb-connect.sh) | 每次 reboot 后重配 RNDIS |
| [reboot-fastboot.sh](../scripts/reboot-fastboot.sh) | 从 Ubuntu 进 fastboot |

验证用 `fastboot boot out/boot.img`，**不要** `fastboot flash boot`。

---

## 0. 一句话结论

wlan0 最终能扫、能关联、能上网、能显示 VHT 速率，不是因为「再等一下 QMI」或「换一份 linux-firmware」，而是叠在一起的 **六件真事**：

1. **WLAN DSP 住在 MPSS 里。** 没有 `rmtfs` + `remoteproc_mpss` 启动，QRTR 上永远没有 WLFW，ath10k 就永远停在「等固件」。  
2. **MSA 是 1 MB，而且必须 hyp_assign。** 下游客给 icnss 的是 `qcom,wlan-msa-memory = <0x100000>`；ath10k 若按 reserved-memory 登 2 MB，固件会走出 TZ 映射。`qcom,msa-fixed-perm` 会跳过 assign，CAL 后约 11 ms 把整个 MPSS 拉死。  
3. **BDF 用本机 `bdf_c3j.bin` 当 `board.bin`。** 不要把 raw BDF 当成 `board-2.bin`，也不要混用 HL.2.0 的 `wlanmdsp.mbn`。  
4. **空 CAL 必须先应答 `CAL_DOWNLOAD`，再发 `CAL_REPORT`。** `cal_done=1` 会让固件干等；空 report 又不答 download 会触发冷启动 RF 校准，约 11 ms 后 `SFR Init: wdog`。工厂 NV 在 modemst（rmtfs），host 侧 cal 文件是空的，这是协议，不是「缺文件」。  
5. **STA 必须先建 BSS peer（AP 的 BSSID），再 `vdev-start`。** mac80211 是先 `assign_vif_chanctx` 再 `sta_state`。主线原来的顺序让 WLAN.HL.3.x 回 `vdev-start status 0xffffffff`。**加 STA self-peer 是错的**——和 `vdev_create` 隐式 self-peer 冲突，固件 `wlan_vdev_free` 断言、modem 循环崩。  
6. **速率显示不能靠 minstrel / HTT PEER_STATS / WMI PEER_STATS_INFO。** 固件 RC，SNOC TLV HTT 没有 PEER_STATS 报文；`REQUEST_PEER_STATS_INFO` 会 3 秒超时。正确做法：记住最近一次 HT/VHT **数据帧** 的 MCS，不要让 beacon 的 6 Mbps 盖掉。

中间还踩过：thermal quiet mode 崩固件、WMI_INIT 广告 chainmask `0x7`、11g 信道乱标 HT/VHT、信道列表改成 HT/VHT80（`halphy_caldb_find_rfsub` 崩）、wifi-setup 里 bounce wlan0 改 MAC。

---

## 1. 心得（给下一次 WCN3990 / HL.3.x bring-up）

### 1.1 把链路拆成「能独立证伪」的几段

```
MPSS 起来  ≠  WLFW QMI 握手  ≠  BDF/CAL 完成  ≠  FW_READY/wlan0
  ≠  scan 有 AP  ≠  vdev-start 成功  ≠  关联  ≠  有 IP  ≠  速率显示正确
```

| 段 | 假阳性 | 真证伪 |
|----|--------|--------|
| 驱动 probe | `ath10k_snoc` bound、四路 regulator 使能 | QRTR 上有 WLFW |
| QMI | `qmi BDF download done` | 之后 **没有** `fatal error: SFR Init`，且出现 `FW_READY` |
| wlan0 | 接口存在 | `iw phy` 有 2.4 + 5 GHz，`iw scan` 看得到目标 AP |
| 关联 | NetworkManager「正在连接」 | `wlan0: associated` + `status=0` + DHCP |
| 速率 | 刚关联 `rx bitrate: 6.0` | 有数据后 **VHT-MCS + 80MHz**，空闲也不掉回 6.0；`tx bitrate` 有字段 |

这次最值钱的证伪是：**QMI 每一步都 success，11 ms 后 MPSS 死**——问题不在「固件没找到」，而在 **host 对 CAL/MSA 的语义和下游 icnss 不一致**。

### 1.2 WLAN.HL.3.x 不是 HL.2.0 加两个 TLV

主线 ath10k 的 WCN3990 路径是按 **HL.2.0 / linux-firmware** 写的。ginkgo 出厂是 **HL.3.0.2**。差别会以「status -1 / 0xffffffff / 整颗 modem 喂狗」出现，而不是 `-EINVAL`。

已经对上、不能回退的 HL.3.x 语义：

| 项 | HL.2.0 主线默认 | ginkgo HL.3.0.2 要求 |
|----|-----------------|----------------------|
| `wlanmdsp.mbn` | linux-firmware `WLAN.HL.2.0-01387` | **本机** `WLAN.HL.3.0.2-00656` |
| WMI_INIT chainmask | 硬编码 `0x7` | 固件真实链数（本机 `0x1`） |
| `vdev_create` | 停在 MAC | `num_cfg_txrx_streams` + 2G/5G `VDEV_TXRX_STREAMS` TLV |
| `vdev_start` | 停在 `disable_hw_ack` | `preferred_tx/rx_streams`；可加 `LDPC_RX` |
| STA vdev-start | chanctx 一 assign 就 start | **BSS peer 已存在** 才能 start |
| beacon/dtim | 允许 0 | 0 会被拒，默认 100 / 1 |
| 信道 flags | 11g 也可能带 HT/VHT | HALPHY 按 phymode 对齐，11g 不许 ALLOW_HT/VHT |
| THERM_THROT quiet | 固件广告了就发 | **发了就崩**（PC ~`0xb0006504`） |

### 1.3 「self-peer」和「BSS peer」不是同一个东西

ath11k / qcacld 的 STA 路径是：先把 **AP 的 BSSID** 加成 BSS peer，再 vdev-start。  
`vdev_create` 已经在固件里建了 **STA 自己 MAC 的 self-peer**。host 再 `peer_create(自己的 MAC)` 会和隐式 peer 撞车 → `wlan_vdev_free` 断言 → MPSS 循环崩。

mac80211 的顺序是：

```
assign_vif_chanctx  →  （主线原来这里 vdev-start）  →  sta_state 才 peer_create(BSSID)
```

所以对 WCN3990 非 AP/非 monitor：**chanctx 里只记下 chandef，延迟 start；`sta_state` 建好 BSS peer 后再 start。** 不要发明第三条路去加 self-peer。

### 1.4 天线数量以 service-ready 为准，不要改 hw_params 去「看起来像 2x2」

`iw phy`：TX/RX antenna `0x1`。`wmi service ready chains 1`。ginkgo 单天线，CH1 供电绑到 CH0 只是让 `ath10k_snoc` bulk-get 不失败。  
把 WMI_INIT / hw_params 写成 `0x3` 或 `0x7`，HALPHY 会在 vdev-start 或 caldb 里炸掉。

### 1.5 速率：6 Mbps 经常是真的，只是不是你想看的那一帧

5 GHz beacon 就是 6 Mbps OFDM。mac80211 的 `rx bitrate` 是 **最后一次 RX**。空闲时最后一帧几乎总是 beacon。  
有 ping/流量之后数据是 VHT80 MCS7 = 292.5 Mbps（1x1 long GI），这才是链路速率。

TX 没有 minstrel 样本（`HAS_RATE_CONTROL` + 固件 RC）。SNOC 的 TLV HTT 表里 **没有** `PEER_STATS`。`WMI PEER_STATS_INFO` 在这颗 HL.3.0.2 上请求会超时。所以驱动必须自己缓存最后一次数据 MCS。

### 1.6 调试通道本身也是 bring-up 的一部分

- 每次 `fastboot boot` / reboot 后先跑 `./scripts/usb-connect.sh`  
- SSH：`root@192.168.7.2`，密码 `$GINKGO_ROOT_PASSWORD`，主机 `192.168.7.1`  
- 进 fastboot：`./scripts/reboot-fastboot.sh`  
- 验证：`fastboot boot out/boot.img`，**不要 flash**  
- 第一次 `fastboot boot` 常失败；**不要 usbreset**（会弄丢 USB）。卡住用 `killall -9 fastboot`  
- `pkill -f 'fastboot boot'` 会误杀当前 shell  
- MPSS 崩之后 ping 还在、sshd 可能死：让人按键进 fastboot，不要对已经 running 的 rproc `echo start`

### 1.7 一次只改一个变量

WiFi 失败实验最多的，都是把两层叠在一起：MSA 大小 + `msa-fixed-perm`、self-peer + 延迟 start、chainmask + 信道列表 HT/VHT80。能复现的改动先单独 `fastboot boot`，再叠下一项。

---

## 2. 目标、验收、非目标

### 2.1 目标

主线 Linux 上走完整：

```
MPSS → WLFW QMI → ath10k_snoc → mac80211 → wlan0 → 2.4G 与 5G 关联 → IPv4
```

并让 `iw` / NetworkManager **显示数据 MCS**，而不是永远 6 Mbps、没有 `tx bitrate`。

### 2.2 量化验收（2026-08-18 实测）

| 指标 | 成功阈值 | 实测 |
|------|----------|------|
| `wlan0` | UP | UP |
| 监管域 | `iw reg get` 为 CN | `ginkgo-wifi-setup.sh` 只 `iw reg reload` + `iw reg set CN` |
| 扫描 | 看得到 `<test-ap-2g>` 与 `<test-ap-5g>` | 是 |
| 关联 | `associated` status=0 | `<test-ap-5g>` aid=7 |
| 信道 | 5 GHz VHT80 | freq 5180，width 80 MHz，center1 5210 |
| IP | DHCP | `<wlan0-dhcp>/24` gw `192.168.1.1` |
| ping | 网关通 | ~15 ms，6/6 |
| 速率（有流量后） | VHT NSS1 80 MHz | **RX/TX 292.5 MBit/s VHT-MCS 7 80MHz VHT-NSS 1** |
| 空闲 | 不掉回 6.0 | 3–4 s 后仍 292.5 |

### 2.3 非目标 / 不要做

- 不要 `fastboot flash boot` / 写 userdata  
- 不要加回 STA self-peer、信道列表改成 HT/VHT80、hw_params chain mask `0x3`  
- 不要加 `qcom,msa-fixed-perm`、`qcom,use-guard-pages`、`qcom,no-msa-ready-indicator`  
- 不要对 running rproc `echo start`  
- 不要把 raw BDF 当 `board-2.bin`  
- 不要混用 linux-firmware 的 `WLAN.HL.2.0` `wlanmdsp.mbn`  
- 不要恢复 GNOME；不要为修 WiFi 去 bounce wlan0 / 改 MAC  
- 不要为 WiFi 回退显示预取、触控 CS、`nt36xxx.h` 的 `#undef CONFIG_FB`

---

## 3. 硬件与软件基线

### 3.1 芯片与供电

| 项 | 值 |
|----|-----|
| 芯片 | WCN3990 |
| 寄存器 | `0x0c800000`，8 MB |
| CE 中断 | GIC SPI **358–369**（下游 `icnss@C800000` CE0–CE11） |
| SMMU SID | **`0x80`**（sm6115 是 `0x1a0`，抄错会 IOMMU fault） |
| MSA | `0x53300000`，**1 MB**；其后 1 MB 是 `wlan_msa_guard`（防走超，不是第二段 MSA） |
| CX/MX | L8A `vdd-0.8-cx-mx` |
| XO | L16A `vdd-1.8-xo` |
| RFA | L17A `vdd-1.3-rfa` |
| CH0 / CH1 | 都接 L23A（单天线；驱动仍 bulk-get ch1） |

### 3.2 固件文件

路径：`/lib/firmware/ath10k/WCN3990/hw1.0/`  
来源：`firmware/ginkgo/wifi/`，由 `scripts/configure-rootfs.sh` 装进 rootfs。

| 文件 | 大小 | 说明 |
|------|------|------|
| `wlanmdsp.mbn` | 3,720,220 B | **本机** HL.3.0.2-00656。MPSS 经 tqftpserv 拉取 |
| `firmware-5.bin` | 60 B | ath10k 特性位描述符。Android CNSS **不需要**；主线需要。可来自 linux-firmware 的 60 字节文件，**不要**连带换 wlanmdsp |
| `board.bin` | 26,328 B | **`bdf_c3j.bin` 原样复制**。ath10k 在 `board-2.bin` 对不上 qmi-board-id 时回退到它 |
| `board-2.bin` | — | **不要装 raw BDF**。那个格式是 QCA-ATH10K-BOARD IE |

`firmware-5.bin` 不是 MAC 固件本体。MAC/PHY 在 `wlanmdsp.mbn` 里。

### 3.3 用户态

| 组件 | 作用 |
|------|------|
| `ginkgo-mpss.service` → `ginkgo-start-mpss.sh` | `echo start` 到 `6080000.remoteproc`（PAS `auto_boot` 为 false） |
| `rmtfs` | 把 modemst 做成 `/dev/qcom_rmtfs_mem1`，工厂 WLAN NV 在这里 |
| `tqftpserv` + `pd-mapper` | 给 MPSS 下 `wlanmdsp.mbn` / `.mdt` |
| `ginkgo-wifi-setup.service` | **只** `iw reg reload` + `iw reg set CN`。禁止 link down/up |

内核 config：`config/ginkgo.fragment` 里 `CONFIG_ATH10K=y`、`CONFIG_ATH10K_SNOC=y`。**不要**开 `CONFIG_WCN36XX`（那是 Prima WCN3660/3680）。

ath10k SNOC 对 HTT 是 **LL**（`ATH10K_DEV_TYPE_LL`）：数据 RX 走描述符，`ath10k_htt_rx_h_rates` 会填 VHT encoding。USB/SDIO 那种 HL RX 指示里往往不填速率，那是另一条路。

---

## 4. 分层排查（按这个顺序，不要跳）

```
L6 用户可见   iw link 有 VHT 速率 + ping 通
L5 mac80211   associated + DHCP
L4 WMI        vdev-start status=0，BSS peer 在 start 之前
L3 ath10k     wlan0 + iw phy 双频
L2 QMI        BDF + CAL_DOWNLOAD 应答 + CAL_REPORT + FW_READY，MPSS 不死
L1 MPSS       remoteproc running，QRTR 有 WLFW
L0 电气/DT    wifi@c800000、SID 0x80、MSA 1MB、四路 regulator
```

| 现象 | 先查 |
|------|------|
| 没有 `wlan0`，dmesg 无 WLFW | rmtfs 节点、`ginkgo-mpss`、`cat remoteproc0/state` |
| QMI 走到 CAP 后 ~50 ms 崩 | MSA 是不是还在 2 MB |
| BDF/CAL done 后 ~11 ms `SFR Init: wdog` | 是不是加了 `msa-fixed-perm`，或空 CAL 没答 `CAL_DOWNLOAD` |
| 卡在等 FW_READY | 是不是 `cal_done=1` 或完全没发 CAL_REPORT |
| `vdev-start` `0xffffffff` / status -1 | 有没有 BSS peer；INIT chainmask 是不是 `0x7`；11g 有没有乱标 HT/VHT |
| 一关联 modem 循环崩 | 是不是又加了 STA self-peer，或改了信道列表 HT/VHT80 |
| 只有 6 Mbps、没有 `tx bitrate` | 先 ping 再看；空闲 6.0 是 beacon。驱动应缓存数据 MCS |
| `iw station dump` 卡 3 秒 | 不要对这颗固件发 `REQUEST_PEER_STATS_INFO` |

---

## 5. 失败实验时间线（避免再走一遍）

按时间，不是按「看起来合理的顺序」。

### 5.1 只有 DT + 本机固件：`ath10k_snoc` bound，没有 wlan0

SM6125 原先没有 `wifi@c800000`（sm6115 有）。补上节点、ginkgo 供电、`board.bin=c3j` 之后，驱动绑上了，四路电源也亮了。QRTR **没有任何 WLFW 节点**。

根因：WCN3990 的 WLAN 固件由 **MPSS** 拉起。PAS 默认 `auto_boot=false`。没有 rmtfs 时 Hexagon 会在开机约 40 s 后 fatal，WLFW 永远不出现。

### 5.2 启动 MPSS 但没有 rmtfs：约 40 s 后崩

`&remoteproc_mpss` + `ginkgo-start-mpss.sh` 不够。必须在 ginkgo DTS 里留：

```
rmtfs_mem@89b01000  2 MB
qcom,client-id = <1>
qcom,vmid = MSS_MSA + NAV
```

地址跟 sm6115 主线惯例，不是随便找一块空闲 DRAM。

### 5.3 MSA 广告 2 MB：CAP 后约 50 ms 崩

下游 icnss 只把 **1 MB** 交给 WLAN。ath10k 用 `memory-region` 的全长。reserved-memory 写成 2 MB 时，WLAN 会走出 TZ MSA 映射。

修法：`wlan_msa_mem` = `0x53300000` / 1 MB；后面 1 MB 当 `wlan_msa_guard` 占位，**不要**交给 wifi 节点。

### 5.4 `qcom,msa-fixed-perm`：CAL 后约 11 ms 崩

主线这个属性会跳过 MSA 的 hyp_assign。下游 `qcom,wlan-msa-fixed-region` **只钉 PA**，icnss 仍然会 assign 给 `MSS_MSA + WLAN + WLAN_CE`。抄属性名不等于抄语义。

日志形态：

```
qmi BDF download done
qmi cal report done
qcom_q6v5_pas: fatal error: SFR Init: wdog or kernel error suspected
```

**不要**加 `qcom,msa-fixed-perm`。

### 5.5 `qcom,no-msa-ready-indicator`

这颗固件 **会** 发 `MSA_READY_IND`。加上这个属性只是把崩提前，根因不变。

### 5.6 `cal_done=1`：永远等不到 FW_READY

本意是「工厂校准已在 EFS，别做冷校准」。HL.3.0.2 的实际行为：`cal_done=1` **抑制** `INITIATE_CAL_DOWNLOAD_IND`，然后固件干等一份它认为应该到来的 `CAL_REPORT`。

正确组合：

- host cap：`bdf_support=1`，`cal_filesys_support=1`，`cal_done=0`  
- 对每个 `CAL_DOWNLOAD_IND` 回 **空** download（`total_size=0`，`end=1`）  
- burst 结束后 delayed work 发 `CAL_REPORT`  
- 工厂 NV 仍由 rmtfs/modemst 提供，不需要 host 侧 cal 文件

空 report 却 **不** 先答 download：固件走冷启动 RF 校准，约 11 ms 后喂狗。完全不发 report：卡在 FW_READY。

### 5.7 混用 linux-firmware `WLAN.HL.2.0` 的 `wlanmdsp.mbn`

和 60 字节 `firmware-5.bin` 不是一套。必须用本机 3.0.2-00656。`firmware-5.bin` 可以是 linux-firmware 那 60 字节（特性位），**wlanmdsp 不行**。

### 5.8 raw BDF 当 `board-2.bin`

`board-2.bin` 需要 QCA-ATH10K-BOARD IE。把 26328 字节 raw 塞进去，ath10k 解析 board-id 会对不上。正确：`cp bdf_c3j.bin board.bin`，不要装 `board-2.bin`（或只装真正转换过的 IE 文件）。

`bdwlan.bin` 是通用板；ginkgo 要用 **c3j** variant 才带齐双频校准。

### 5.9 thermal quiet mode

固件广告 `THERM_THROT`。主线于是发 `pdev_set_quiet_mode`。HL.3.x 在 `wlan_process` 里崩（HL.3.0.2 PC ~`0xb0006504`，HL.3.2 ~`0xb0008e20`）。WCN3990 上 **直接 return，不要发**。

### 5.10 WMI_INIT chainmask `0x7` → vdev-start status -1

主线 TLV INIT 写死 `tx/rx_chain_mask = 0x7`（3x3）。service-ready 说 chains=1。HALPHY 在 vdev-start 用这份 INIT 能力，回 -1。  
修法：WCN3990 按 `num_rf_chains` 发 mask（本机 `0x1`）。**不要**改 `hw_params` 的 `0x7` 去假装 2x2。

### 5.11 11g 信道带 ALLOW_HT/VHT

主线曾不管 phymode 都标 HT/VHT。HALPHY 拒绝对 11g/11a 开这些 flag。flags 必须跟 `chan_to_phymode()` 对齐。

STA 在 `BSS_CHANGED_BEACON_INT` 之前就会 vdev-start：`bcn_intval/dtim` 为 0 也会被 HL.3.x 拒。默认 100 / 1。

### 5.12 STA self-peer：modem 循环崩

为了满足「start 前要有 peer」，曾经 `peer_create(STA 自己的 MAC)`。固件在 `vdev_create` 里已经有 self-peer。冲突 → `wlan_vdev_free` 断言 → MPSS 反复 fatal。**必须撤回，永远不要加回。**

正确：延迟 vdev-start 到 BSS peer（BSSID）创建之后。

### 5.13 信道列表改成 HT/VHT80：`halphy_caldb_find_rfsub` 崩

想「广告 80 MHz 能力」。固件 caldb 按 1x1 / 本机 BDF 建表，host 乱改 channel list 会踩空。能力以 service-ready 为准（本机本来就能 VHT80 NSS1）。

### 5.14 `ginkgo-wifi-setup.sh` bounce wlan0 / 改 MAC

link down/up 和 ath10k scan 竞态，HL.3.x bring-up 期会崩。脚本只许 `iw reg reload` + `iw reg set CN`。

### 5.15 `WMI PEER_STATS_INFO` 查 TX 速率：第一次 `iw` 卡 3 秒

`pdev_param peer_stats_info_enable=1` 这颗固件吞得下去（无 warn）。`REQUEST_PEER_STATS_INFO` **没有事件回来**，`wait_for_completion_timeout` 3 s。超时后必须关掉这条路径，否则每次 dump 卡死。  
TX 速率改走「最后一次数据 RX MCS 缓存」。1x1 上 TX/RX MCS 同量级，足够 `iw` / NM 显示。

---

## 6. 决定性修复（代码级）

### 6.1 DT：wifi 节点 + 1 MB MSA + 不要 msa-fixed-perm

`sm6125.dtsi`：`wifi@c800000`，`compatible = "qcom,wcn3990-wifi"`，CE 中断 358–369，`iommus = <&apps_smmu 0x80 0x1>`，`memory-region = <&wlan_msa_mem>`（1 MB）。

`sm6125-xiaomi-ginkgo.dts`：`&wifi` 五路 supply（ch1=ch0）、`qcom,calibration-variant = "Xiaomi_ginkgo"`、`status = "okay"`。`rmtfs_mem@89b01000`。`&remoteproc_mpss` 指向本机 `modem.mdt`。

### 6.2 QMI host cap + 空 CAL 协议

`ath10k_qmi_host_cap_send_sync`：

- `bdf_support = 1`  
- `cal_filesys_support = 1`  
- `cal_done = 0`  

`CAL_DOWNLOAD_IND` → 空 download → delayed `CAL_REPORT`。

### 6.3 WMI_INIT chainmask 跟固件链数

`wmi-tlv.c` `gen_init`：WCN3990 用 `num_rf_chains` 生成 mask，实机日志：

```
wmi tlv init chainmask tx 0x1 rx 0x1 fw_chains 1
```

### 6.4 vdev_create / vdev_start 的 HL.3.x TLV

- `vdev_create`：`num_cfg_txrx_streams=2` + 2G/5G `VDEV_TXRX_STREAMS`  
- `vdev_start`：整颗 `wmi_tlv_vdev_start_cmd`（含 `preferred_tx/rx_streams`），NSS 钳在 1..2  
- flags：`WMI_VDEV_START_LDPC_RX_ENABLED (1<<3)`（若 HT LDPC 能力置位）  
- 信道 ALLOW_HT/VHT/ht40plus 跟 phymode 走

### 6.5 延迟 vdev-start 到 BSS peer 之后

`ath10k_wcn3990_delay_vdev_start()`：WCN3990 且非 AP/非 monitor。

- `assign_vif_chanctx`：若还没有 peer，只存 `delayed_chandef`，return 0  
- `sta_state` 里 `peer_create(BSSID)` 成功后：`ath10k_mac_finish_delayed_vdev_start()`  
- 删 peer 前若还在 delayed，先 stop  

**禁止** `peer_create(vif->addr)`。

### 6.6 跳过 quiet mode

`thermal.c`：`QCA_REV_WCN3990(ar)` 则不发 quiet。

### 6.7 速率：缓存最后一次 HT/VHT 数据 RX

`htt_rx.c` `ath10k_process_rx`：data + HT/VHT → `arsta->last_data_rxrate`。  
`mac.c` `ath10k_sta_statistics`：不要在 `!peer_stats_enabled` 时直接 return；用这份缓存填 RX，TX 若仍是 legacy/空则用同一份。beacon 6 Mbps 不再盖掉 VHT。

不要为 WCN3990 打开 `supports_peer_stats_info`（会 3 s 超时）。

---

## 7. 验收日志

### 7.1 QMI / 固件（健康）

```
ath10k_snoc c800000.wifi: wmi service ready chains 1 ht 0x381b vht 0x739011b2
wmi tlv init chainmask tx 0x1 rx 0x1 fw_chains 1
```

QMI：`chip_id 0x120`，`board_id 0xff`。BDF 26328 字节。CAL download/report 之后 **FW_READY**，没有紧跟着的 `SFR Init: wdog`。

### 7.2 关联 `<test-ap-5g>`

```
wlan0: authenticated
wlan0: RX AssocResp ... status=0 aid=7
wlan0: associated
Device wlan0 successfully activated
SSID: <test-ap-5g>  freq 5180  width 80 MHz  center1 5210
IP: <wlan0-dhcp>/24  gw 192.168.1.1
```

延迟 start 日志：

```
wcn3990 defer vdev 0 start until BSS peer
wcn3990 delayed vdev 0 start after BSS peer
```

### 7.3 速率（ping 网关之后）

```
rx bitrate: 292.5 MBit/s VHT-MCS 7 80MHz VHT-NSS 1
tx bitrate: 292.5 MBit/s VHT-MCS 7 80MHz VHT-NSS 1
```

刚关联、还没有任何数据时，仍可能看到 6.0（beacon）。ping 或打开网页后应升到 VHT，空闲保持。`dmesg` **不应**出现 `timed out waiting peer stats info`。

292.5 Mbps = 1x1 VHT80 MCS7 long GI（表项 2925×100 kbps）。SGI 会是 325.0。这是单天线的正确上限附近，不是「少了一根天线的 bug」。

---

## 8. 关键文件

| 项 | 路径 |
|----|------|
| SoC wifi 节点 / MSA | `linux/arch/arm64/boot/dts/qcom/sm6125.dtsi` |
| ginkgo 供电 / rmtfs / mpss | `linux/arch/arm64/boot/dts/qcom/sm6125-xiaomi-ginkgo.dts` |
| 延迟 vdev-start、信道 flags、sta_statistics | `linux/drivers/net/wireless/ath/ath10k/mac.c` |
| `vdev_start_delayed` | `linux/drivers/net/wireless/ath/ath10k/core.h` |
| INIT chainmask、vdev TLV | `linux/drivers/net/wireless/ath/ath10k/wmi-tlv.c` |
| HL.3.x 结构体 / LDPC flag | `linux/drivers/net/wireless/ath/ath10k/wmi-tlv.h`、`wmi.h` |
| QMI host cap / CAL | `linux/drivers/net/wireless/ath/ath10k/qmi.c` |
| 跳过 quiet | `linux/drivers/net/wireless/ath/ath10k/thermal.c` |
| 数据 MCS 缓存 | `linux/drivers/net/wireless/ath/ath10k/htt_rx.c` |
| 内核 fragment | `config/ginkgo.fragment` |
| 固件安装 | `scripts/configure-rootfs.sh` |
| 本机固件 | `firmware/ginkgo/wifi/` |
| 启动 MPSS | `rootfs-overlay/usr/local/sbin/ginkgo-start-mpss.sh` |
| 监管域 | `rootfs-overlay/usr/local/sbin/ginkgo-wifi-setup.sh` |

---

## 9. 怎么验证（回归）

```bash
source scripts/env.sh
./scripts/build-kernel.sh
./scripts/build-bootimg.sh
# 手机在 Ubuntu 且 SSH 好：
./scripts/reboot-fastboot.sh
fastboot boot out/boot.img
# 起来后：
./scripts/usb-connect.sh
./scripts/ssh-run.sh \
  'iw dev wlan0 link; ping -c 6 -W 2 192.168.1.1; iw dev wlan0 link; iw dev wlan0 station dump | grep bitrate'
```

期望：第二次 `iw link` 同时有 `rx bitrate` 和 `tx bitrate` 的 VHT-MCS 行。

测试 AP：`<test-ap-5g>`（5 GHz），2.4G 同密 `<test-ap-2g>`。不要在脚本里写死密码。

---

## 10. 和显示 / 触控 bring-up 的对照

| | 显示 P3 | 触控 P4 | WiFi P5（本文件） |
|--|---------|---------|-------------------|
| 假阳性 | 背光亮、DCS complete、INTF 60fps | event 节点在、chip ID 在 | QMI BDF/CAL success、wlan0 UP、rx 6.0 |
| 真证伪 | DSI TPG 上屏；FIFO `0x1010` | `Update firmware success` + 出点 | associated + ping + **VHT 速率不掉 6.0** |
| 主线默认值坑 | prog fetch 24 vs VFP 10 | DT `spi-cs-high` vs MODE_0 | HL.2.0 WMI/QMI vs 本机 HL.3.0.2 |
| 抄下游要抄语义 | LCDB 不是 LAB/IBB | 驱动覆盖 CS，不是 DT 字面 | `msa-fixed-region` ≠ `msa-fixed-perm` |
| 崩了之后 | USB 可能还在 | sshd 常死，人进 fastboot | MPSS 循环崩同样要人进 fastboot |

显示和触控已经通了。修 WiFi **不要**去动那两棵树上已验证的根因。

---

## 11. 不要再做的清单（给未来的自己）

1. 不要加回 STA **self-peer**  
2. 不要把信道列表改成 HT/VHT80 去「开 80 MHz」  
3. 不要把 `hw_params` chain mask 改成 `0x3`  
4. 不要 `qcom,msa-fixed-perm` / `qcom,use-guard-pages` / `no-msa-ready-indicator`  
5. 不要 MSA 2 MB  
6. 不要 linux-firmware 的 HL.2.0 `wlanmdsp.mbn`  
7. 不要 raw BDF 当 `board-2.bin`  
8. 不要 WCN3990 quiet mode  
9. 不要 INIT chainmask `0x7`  
10. 不要在 wifi-setup 里 bounce wlan0 / 改 MAC  
11. 不要对这颗固件 `REQUEST_PEER_STATS_INFO`  
12. 不要 `fastboot flash boot`，除非用户明确说写分区  
13. 不要 usbreset 救 `fastboot boot`  
14. 不要对已经 running 的 rproc `echo start`

---

*记录结束。WiFi 验收日期 2026-08-18。*
