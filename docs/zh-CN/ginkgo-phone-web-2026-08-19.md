**语言：** [English](../ginkgo-phone-web-2026-08-19.md) | 简体中文

# ginkgo 公网站点：`*.phone.example.com`

> 设备：Redmi Note 8 · **ginkgo** · SM6125  
> 验收日：2026-08-19  
> 目标：VPS 管证书，手机只收 HTTP 透传

本文已脱敏：真实域名、VPS 地址、面板入口路径不入库。本地仍按你自己的 DNS / NPS 配置。

## 一句话

公网访问 `https://test.phone.example.com` 已通。TLS 在云 VPS（`example.com` / `<VPS_IP>`）终结，明文 HTTP 经 NPS `8090 → 手机:80`，由机上 nginx / 宝塔站点响应。

```
Internet
  → VPS OpenResty :80/:443   （证书、HTTP→HTTPS、ACME）
  → 127.0.0.1:8090           （NPS TCP，不对公网开放）
  → 手机 nginx :80           （/www/wwwroot/test.phone.example.com）
```

## DNS

`phone.example.com`、`test.phone.example.com`、`*.phone.example.com` 都已指到 `<VPS_IP>`。

本机若开了 Mihomo fake-ip，`dig` / 浏览器会看到 `198.18.x`。测真实链路用：

```bash
curl -I --resolve test.phone.example.com:443:<VPS_IP> https://test.phone.example.com/
```

## 证书（只在 VPS）

- Let’s Encrypt：`test.phone.example.com` + `phone.example.com` + `panel.phone.example.com` + `alist.phone.example.com`
- 路径：`/etc/letsencrypt/live/<cert-name>/`
- 同步到 OpenResty：`/opt/1panel/apps/openresty/openresty/www/sites/phone.example.com/ssl/`
- 续期 hook：`/etc/letsencrypt/renewal-hooks/deploy/phone-example.sh`
- 站点 conf：`/opt/1panel/apps/openresty/openresty/conf/conf.d/phone.example.com.conf`

`test.phone` / `phone` / `panel.phone` 走 HTTPS（80 会 301）。其它 `*.phone` 子域目前只有 HTTP；通配符证书需要 DNS-01（DNSPod / 腾讯云 DNS API）。

手机 **不要** 再申请证书，也不要在 80 上做 HTTPS。

## 手机软件

| 组件 | 状态 |
|------|------|
| Docker | 已有 29.7.2 |
| 宝塔面板 | 已装，监听 `:33144` |
| nginx | Ubuntu 包 **1.28.3**（官方宝塔编译脚本在 Ubuntu 26.04 缺 `libpcre3`，改用系统包） |
| OpenList | Docker **v4.2.5**（AList 社区续作），本机 `127.0.0.1:5244`，公网 https://alist.phone.example.com ；账号在 `/root/alist-credentials.txt` |

站点目录：`/www/wwwroot/test.phone.example.com`  
vhost：`/www/server/panel/vhost/nginx/test.phone.example.com.conf`  
系统 nginx 已 `include` 上述 vhost。

面板账号在手机 `/root/bt-credentials.txt`（权限 600，不要提交 git）。

公网：`https://panel.phone.example.com/<panel-entry>`（VPS 证书，NPS `8091 → 33144`）

局域网（自签证书）：

- USB：`https://192.168.7.2:33144/<panel-entry>`
- WiFi：`https://<当时 wlan0 地址>:33144/<panel-entry>`

必须带安全入口路径，否则宝塔会伪装成 nginx 404。宝塔安装脚本会装 ufw 并拦 22，已关掉；不要再开 ufw，除非先放行 22/80。

## NPS

| 隧道 | 公网/本机 | 手机 |
|------|-----------|------|
| ginkgo-ssh | VPS `:8089` | `:22` |
| ginkgo-http80 | VPS `127.0.0.1:8090` | `:80` |
| ginkgo-btpanel | VPS `127.0.0.1:8091` | `:33144` |

8090 / 8091 只给 OpenResty 反代，安全组不必对公网开放。

## 不要做

- 不要把 VPS 的 80/443 交给 nps（1Panel OpenResty 占用）
- 不要占用 8088/8089
- 不要把宝塔密码、NPS vkey 写进仓库
