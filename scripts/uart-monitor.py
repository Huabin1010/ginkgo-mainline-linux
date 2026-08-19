#!/usr/bin/env python3
"""Real-time UART monitor for ginkgo debug console.

Prints to terminal and saves to backup/ginkgo/logs/ automatically.
Uses only Python stdlib (no pyserial required).

Usage:
  python3 scripts/uart-monitor.py
  python3 scripts/uart-monitor.py /dev/ttyACM0
  python3 scripts/uart-monitor.py -b 115200 -o /tmp/uart.log
"""

from __future__ import annotations

import argparse
import datetime as dt
import glob
import os
import select
import sys
import termios
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_LOG_DIR = ROOT / "backup" / "ginkgo" / "logs"
DEFAULT_CANDIDATES = (
    "/dev/ttyACM0",
    "/dev/ttyACM1",
    "/dev/ttyUSB0",
    "/dev/ttyUSB1",
)

BAUD_MAP = {
    9600: termios.B9600,
    19200: termios.B19200,
    38400: termios.B38400,
    57600: termios.B57600,
    115200: termios.B115200,
    230400: termios.B230400,
    460800: termios.B460800,
    921600: termios.B921600,
}


def find_device(explicit: str | None) -> str:
    if explicit:
        if not os.path.exists(explicit):
            raise SystemExit(f"设备不存在: {explicit}")
        return explicit

    for path in DEFAULT_CANDIDATES:
        if os.path.exists(path):
            return path

    found = sorted(glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*"))
    if len(found) == 1:
        return found[0]
    if found:
        raise SystemExit(
            "发现多个串口设备，请指定其一：\n  " + "\n  ".join(found)
        )

    raise SystemExit(
        "未找到串口设备。请插入 USB-TTL 后重试，或手动指定：\n"
        "  python3 scripts/uart-monitor.py /dev/ttyACM0"
    )


def default_log_path(log_dir: Path) -> Path:
    log_dir.mkdir(parents=True, exist_ok=True)
    stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    return log_dir / f"uart-{stamp}.log"


def configure_port(fd: int, baud: int) -> None:
    if baud not in BAUD_MAP:
        supported = ", ".join(str(b) for b in sorted(BAUD_MAP))
        raise SystemExit(f"不支持的波特率 {baud}，可选: {supported}")

    attrs = termios.tcgetattr(fd)
    attrs[0] &= ~(
        termios.IGNBRK
        | termios.BRKINT
        | termios.PARMRK
        | termios.ISTRIP
        | termios.INLCR
        | termios.IGNCR
        | termios.ICRNL
        | termios.IXON
    )
    attrs[1] &= ~termios.OPOST
    attrs[2] &= ~(termios.CSIZE | termios.PARENB)
    attrs[2] |= termios.CS8
    attrs[3] &= ~(termios.ECHO | termios.ECHONL | termios.ICANON | termios.ISIG | termios.IEXTEN)
    attrs[4] = attrs[5] = BAUD_MAP[baud]
    attrs[6][termios.VMIN] = 0
    attrs[6][termios.VTIME] = 1
    termios.tcsetattr(fd, termios.TCSANOW, attrs)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="ginkgo UART 实时监听 + 自动保存"
    )
    parser.add_argument(
        "device",
        nargs="?",
        help="串口设备，如 /dev/ttyACM0（默认自动检测）",
    )
    parser.add_argument(
        "-b",
        "--baud",
        type=int,
        default=115200,
        help="波特率（默认 115200）",
    )
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        help="日志文件路径（默认 backup/ginkgo/logs/uart-时间戳.log）",
    )
    parser.add_argument(
        "--log-dir",
        type=Path,
        default=DEFAULT_LOG_DIR,
        help=f"日志目录（默认 {DEFAULT_LOG_DIR}）",
    )
    args = parser.parse_args()

    device = find_device(args.device)
    log_path = args.output or default_log_path(args.log_dir)

    print(f"设备: {device}")
    print(f"波特率: {args.baud}")
    print(f"日志: {log_path}")
    print("先开监听，再开机/重启手机。按 Ctrl+C 停止。\n")

    try:
        fd = os.open(device, os.O_RDONLY | os.O_NOCTTY)
    except OSError as exc:
        print(f"无法打开串口: {exc}", file=sys.stderr)
        print(
            "若无权限，可执行: sudo usermod -aG dialout $USER 后重新登录，"
            "或用 sudo 运行本脚本。",
            file=sys.stderr,
        )
        return 1

    configure_port(fd, args.baud)

    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(line_buffering=True)

    try:
        with log_path.open("ab", buffering=0) as log_file:
            while True:
                ready, _, _ = select.select([fd], [], [], 0.2)
                if not ready:
                    continue

                chunk = os.read(fd, 4096)
                if not chunk:
                    continue

                log_file.write(chunk)
                sys.stdout.write(chunk.decode("utf-8", errors="replace"))
                sys.stdout.flush()
    except KeyboardInterrupt:
        print("\n\n已停止监听。")
        print(f"日志已保存: {log_path}")
        return 0
    finally:
        os.close(fd)


if __name__ == "__main__":
    raise SystemExit(main())
