#!/usr/bin/env python3
"""Bake Noto Sans CJK SC glyphs into initramfs/ginkgo-status-font.h."""
from __future__ import annotations

import os
import sys

sys.path.insert(0, "/usr/lib/python3/dist-packages")
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "initramfs", "ginkgo-status-font.h")
SRC = os.path.join(ROOT, "initramfs", "ginkgo-status.c")
PREVIEW = os.path.join(ROOT, "out", "ginkgo-status-font-preview.png")
TTC = "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"

UI = """
Redmi Note 8 Ubuntu Wi-Fi CPU GPU Kryo Adreno MHz
已连接 未连接 尚未连接无线网络 点此选择网络并连接
SSID IP 地址 信号 频率 网关 接口 选择网络
占用 未就绪 音量 进入 Fastboot 可用网络 扫描中 刷新 返回
无可用网络 锁定 开放 输入密码 连接 取消 连接中 连接失败
加密 无密码 空闲 已就绪 频段 WPA WEP
内存 处理器 显卡 当前 状态 设备 强度 信道 隐藏 请稍候
正在扫描 正在连接 连接成功 无线网卡 请打开 短按 电源
提示 用户 相关 信息 显示 启动 删除 Shift ABC
运行 已运行 秒 开机 已连
亮度 中文
功耗 充电 放电 电池
关闭屏幕 上行 下行 息屏
找到 电源键 请检查
上传 下载
网络详情 忘记此网络 加入此网络 正在忘记 已忘记 忘记失败
安全性 已加入 忘记中 确认
密码不对 请再试一次 需要密码 请检查密码
找不到这个网络 连接超时 网卡不匹配 没拿到地址
未能加入此网络 已回到 确认连接
"""

COMMON = (
    "的一是不了人我在有他这为之大来以个中上们到说国和地也子时道出而要于就下得可你年生"
    "自会那后能对着事其里所去行过家十用发天如然作方成者多日都三小军二无同么经法当起与"
    "好看学进种将还分此心前面又定见只主没公从时现理些制心水实加性外市高资工克各次"
    "电手表气命西化区民解意项口由身数利很相因第公门任重给几社知正保或什再平女"
    "美间场部头等反位入内机指第体做新力完科信北原万话题感"
)

ASCII = "".join(chr(i) for i in range(32, 127))
EXTRA = "…·—–°%‰℃±×÷≈≠≤≥•●○★☆【】「」『』（）”“‘’、，。！？：；"


def cps():
    s = set(UI + COMMON + ASCII + EXTRA)
    try:
        src = open(SRC, encoding="utf-8").read()
        s.update(c for c in src if ord(c) > 127)
    except OSError:
        pass
    return sorted(ord(c) for c in s if c not in "\n")


def raster(font: ImageFont.FreeTypeFont, cp: int, px: int, ascent: int, descent: int):
    ch = chr(cp)
    adv = int(round(font.getlength(ch))) or (px if cp > 127 else max(px // 3, 1))
    if ch == " ":
        return {
            "cp": cp,
            "w": 0,
            "h": 0,
            "adv": adv,
            "xoff": 0,
            "yoff": 0,
            "bits": b"",
        }
    pad = 8
    line_top = pad
    baseline = pad + ascent
    canvas_w = max(px * 3, adv + pad * 2, 16)
    canvas_h = pad + ascent + descent + pad
    img = Image.new("L", (canvas_w, canvas_h), 0)
    # Left-baseline so '.' '-' '%' sit on the same line as letters.
    ImageDraw.Draw(img).text((pad, baseline), ch, font=font, fill=255, anchor="ls")
    data = img.tobytes()
    w = canvas_w
    ys = [y for y in range(canvas_h) if any(data[y * w + x] > 12 for x in range(w))]
    xs = [x for x in range(w) if any(data[y * w + x] > 12 for y in range(canvas_h))]
    if not xs or not ys:
        return {
            "cp": cp,
            "w": 0,
            "h": 0,
            "adv": adv,
            "xoff": 0,
            "yoff": 0,
            "bits": b"",
        }
    nx0, nx1 = xs[0], xs[-1] + 1
    ny0, ny1 = ys[0], ys[-1] + 1
    nw, nh = nx1 - nx0, ny1 - ny0
    bits = bytes(data[y * w + x] for y in range(ny0, ny1) for x in range(nx0, nx1))
    return {
        "cp": cp,
        "w": nw,
        "h": nh,
        "adv": adv,
        "xoff": nx0 - pad,
        "yoff": ny0 - line_top,
        "bits": bits,
    }


def dump_face(name: str, px: int, index: int, codes):
    font = ImageFont.truetype(TTC, px, index=index)
    ascent, descent = font.getmetrics()
    glyphs = []
    blob = bytearray()
    for cp in codes:
        g = raster(font, cp, px, ascent, descent)
        off = len(blob)
        blob.extend(g["bits"])
        glyphs.append((g, off))
    lines = [f"static const uint8_t {name}_bits[] = {{"]
    row = []
    for i, b in enumerate(blob):
        row.append(str(b))
        if len(row) == 16:
            lines.append("\t" + ",".join(row) + ",")
            row = []
    if row:
        lines.append("\t" + ",".join(row) + ",")
    lines.append("};")
    lines.append(f"static const struct gs_glyph {name}_glyphs[] = {{")
    for g, off in glyphs:
        lines.append(
            f"\t{{ {g['cp']}, {g['w']}, {g['h']}, {g['adv']}, {g['xoff']}, {g['yoff']}, {off} }},"
        )
    lines.append("};")
    lines.append(
        f"static const struct gs_font {name} = {{ {px}, {ascent}, {descent}, "
        f"{len(glyphs)}, {name}_glyphs, {name}_bits }};"
    )
    return "\n".join(lines) + "\n", glyphs, bytes(blob), px


def blit(img: Image.Image, font_px, glyphs, blob, x, y, text, fill):
    lookup = {g["cp"]: (g, off) for (g, off) in glyphs}
    px = x
    for ch in text:
        cp = ord(ch)
        if cp not in lookup:
            px += font_px // 2
            continue
        g, off = lookup[cp]
        if g["w"] and g["h"]:
            tile = Image.frombytes("L", (g["w"], g["h"]), blob[off : off + g["w"] * g["h"]])
            pos = (px + g["xoff"], y + g["yoff"])
            col = Image.new("RGBA", tile.size, fill + (0,))
            col.putalpha(tile)
            img.alpha_composite(col, pos)
        px += g["adv"]


def preview(faces):
    img = Image.new("RGBA", (1080, 900), (11, 14, 20, 255))
    y = 40
    samples = [
        ("title", "XiaoMi 5"),
        ("body", "Ubuntu 26.04  ·  gemini"),
        ("title", "Wi-Fi"),
        ("num", "37%  0%  12%"),
        ("body", "电源键关闭屏幕 · 找到 3 个网络"),
        ("body", "连接失败，请检查密码"),
    ]
    for name, s in samples:
        glyphs, blob, px = faces[name]
        blit(img, px, glyphs, blob, 48, y, s, (245, 248, 252))
        y += px + 18
    os.makedirs(os.path.dirname(PREVIEW), exist_ok=True)
    img.convert("RGB").save(PREVIEW)
    print("preview", PREVIEW)


def main():
    codes = cps()
    body = dump_face("font_body", 32, 2, codes)
    title = dump_face("font_title", 44, 2, codes)
    num = dump_face("font_num", 60, 2, sorted(ord(c) for c in "0123456789%./:-"))
    parts = [
        "/* SPDX-License-Identifier: GPL-2.0-only */",
        "/* Generated by scripts/gen-gemini-status-font.py — do not edit. */",
        "#ifndef GEMINI_STATUS_FONT_H",
        "#define GEMINI_STATUS_FONT_H",
        "#include <stdint.h>",
        "struct gs_glyph {",
        "\tuint32_t cp;",
        "\tuint16_t w, h, adv;",
        "\tint16_t xoff, yoff;",
        "\tuint32_t off;",
        "};",
        "struct gs_font {",
        "\tint px, ascent, descent, n;",
        "\tconst struct gs_glyph *g;",
        "\tconst uint8_t *bits;",
        "};",
        "",
        body[0],
        title[0],
        num[0],
        "#endif",
        "",
    ]
    with open(OUT, "w") as f:
        f.write("\n".join(parts))
    faces = {
        "body": (body[1], body[2], body[3]),
        "title": (title[1], title[2], title[3]),
        "num": (num[1], num[2], num[3]),
    }
    preview(faces)
    # sanity: CJK must be near font size, not 10px slivers
    g_wei = next(g for g, _ in title[1] if g["cp"] == ord("未"))
    g_w = next(g for g, _ in title[1] if g["cp"] == ord("W"))
    g_dot = next(g for g, _ in body[1] if g["cp"] == ord("."))
    g_pct = next(g for g, _ in num[1] if g["cp"] == ord("%"))
    g_hy = next(g for g, _ in title[1] if g["cp"] == ord("-"))
    print(f"wrote {OUT}")
    print(f"未 {g_wei['w']}x{g_wei['h']} adv={g_wei['adv']} yoff={g_wei['yoff']}")
    print(f"W  {g_w['w']}x{g_w['h']} adv={g_w['adv']} yoff={g_w['yoff']}")
    print(f".  {g_dot['w']}x{g_dot['h']} yoff={g_dot['yoff']}")
    print(f"%  {g_pct['w']}x{g_pct['h']} yoff={g_pct['yoff']}")
    print(f"-  {g_hy['w']}x{g_hy['h']} yoff={g_hy['yoff']}")
    if g_wei["h"] < 28 or g_w["h"] < 20:
        raise SystemExit("glyph too small — bake still clipped")
    if g_dot["yoff"] < 16:
        raise SystemExit("period yoff too small — still sitting on the line top")
    cps_num = [g["cp"] for g, _ in num[1]]
    if cps_num != sorted(cps_num):
        raise SystemExit("font_num not sorted; binary search will miss %")


if __name__ == "__main__":
    main()
