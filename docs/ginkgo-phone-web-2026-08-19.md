**Language:** English | [简体中文](zh-CN/ginkgo-phone-web-2026-08-19.md)

# ginkgo public web sites: `*.phone.example.com`

> Device: Redmi Note 8 · **ginkgo** · SM6125  
> Acceptance date: 2026-08-19  
> Goal: the VPS owns the certificates; the phone only receives plain HTTP

## In one sentence

Public access to `https://test.phone.example.com` works. TLS terminates on a Tencent Cloud VPS (`example.com` / `<VPS_IP>`). Plain HTTP is forwarded by NPS `8090 → phone:80` and answered by nginx / the Baota site on the phone.

```
Internet
  → VPS OpenResty :80/:443   (certs, HTTP→HTTPS, ACME)
  → 127.0.0.1:8090           (NPS TCP, not exposed to the public internet)
  → phone nginx :80          (/www/wwwroot/test.phone.example.com)
```

## DNS

`phone.example.com`, `test.phone.example.com`, and `*.phone.example.com` all point at `<VPS_IP>`.

If the workstation has Mihomo fake-ip enabled, `dig` / the browser will see `198.18.x`. To test the real path:

```bash
curl -I --resolve test.phone.example.com:443:<VPS_IP> https://test.phone.example.com/
```

## Certificates (VPS only)

- Let’s Encrypt: `test.phone.example.com` + `phone.example.com` + `panel.phone.example.com` + `alist.phone.example.com`
- Path: `/etc/letsencrypt/live/<cert-name>/`
- Synced to OpenResty: `/opt/1panel/apps/openresty/openresty/www/sites/phone.example.com/ssl/`
- Renew hook: `/etc/letsencrypt/renewal-hooks/deploy/phone-example.sh`
- Site conf: `/opt/1panel/apps/openresty/openresty/conf/conf.d/phone.example.com.conf`

`test.phone` / `phone` / `panel.phone` use HTTPS (port 80 301s). Other `*.phone` subdomains are HTTP-only for now; a wildcard certificate needs DNS-01 (DNSPod / Tencent Cloud DNS API).

The phone must **not** request certificates, and must **not** serve HTTPS on port 80.

## Software on the phone

| Component | Status |
|------|------|
| Docker | Already present, 29.7.2 |
| Baota panel | Installed, listening on `:33144` |
| nginx | Ubuntu package **1.28.3** (the official Baota compile script lacks `libpcre3` on Ubuntu 26.04, so the distro package is used) |
| OpenList | Docker **v4.2.5** (AList community continuation), local `127.0.0.1:5244`, public https://alist.phone.example.com ; credentials in `/root/alist-credentials.txt` |

Site directory: `/www/wwwroot/test.phone.example.com`  
vhost: `/www/server/panel/vhost/nginx/test.phone.example.com.conf`  
System nginx already `include`s that vhost.

Panel credentials are on the phone at `/root/bt-credentials.txt` (mode 600; do not commit to git).

Public: `https://panel.phone.example.com/<panel-entry>` (VPS certificate, NPS `8091 → 33144`)

LAN (self-signed certificate):

- USB: `https://192.168.7.2:33144/<panel-entry>`
- WiFi: `https://<wlan0 address at the time>:33144/<panel-entry>`

The security-entrance path is required; otherwise Baota impersonates an nginx 404. The Baota install script installs ufw and blocks 22; that has been disabled. Do not re-enable ufw unless 22/80 are allowed first.

## NPS

| Tunnel | Public / local | Phone |
|------|-----------|------|
| ginkgo-ssh | VPS `:8089` | `:22` |
| ginkgo-http80 | VPS `127.0.0.1:8090` | `:80` |
| ginkgo-btpanel | VPS `127.0.0.1:8091` | `:33144` |

8090 / 8091 are only for the OpenResty reverse proxy; the security group does not need to expose them to the public internet.

## Do not

- Do not hand VPS ports 80/443 to nps (1Panel OpenResty owns them)
- Do not occupy 8088/8089
- Do not put Baota passwords or the NPS vkey in the repository
