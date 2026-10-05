#!/usr/bin/env python3
"""限定猜拳的电脑服务（hub）：设备经现场 Wi-Fi 连到这里联机，手机扫码登记昵称，浏览器看板实时显示与记录。

只用 Python 3.9+ 标准库，不需要安装任何依赖：

  python3 tools/kj_hub/hub.py                  # 启动，并在浏览器打开看板
  python3 tools/kj_hub/hub.py --no-browser     # 只启动服务
  python3 tools/kj_hub/hub.py --sweep 192.168.1.0/24   # 路由器不转发广播时，定期逐个通知本网段的设备

端口：UDP 47101（设备）、TCP 47103（庄家看板）、HTTP 47180（看板 / 登记页）。
数据目录默认 ~/kj-hub-data（昵称、对局记录），里面有个人数据，不要放进仓库。
"""

from __future__ import annotations

import argparse
import ipaddress
import logging
import logging.handlers
import socket
import sys
import threading
import webbrowser
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))

import wire  # noqa: E402
from core import HubCore  # noqa: E402
from netio import NetLoop  # noqa: E402
from store import Store  # noqa: E402
from web import HubWeb  # noqa: E402

BOARD_HTML = HERE.parent / "kj_board" / "index.html"


def lan_ip() -> str:
    """本机在局域网里的地址（不会真的发包）。"""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("10.255.255.255", 1))
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()


def local_ips() -> set[str]:
    ips = {"127.0.0.1", lan_ip()}
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            ips.add(info[4][0])
    except OSError:
        pass
    return ips


def broadcast_addrs(ip: str, subnet: str | None) -> list[str]:
    out = ["255.255.255.255"]
    try:
        net = ipaddress.ip_network(subnet, strict=False) if subnet else ipaddress.ip_network(f"{ip}/24", strict=False)
        out.append(str(net.broadcast_address))   # 定向广播：有些路由器只转发这种
    except ValueError:
        pass
    return out


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--data", type=Path, default=Path("~/kj-hub-data"), help="数据目录（默认 ~/kj-hub-data）")
    ap.add_argument("--bind", default="0.0.0.0")
    ap.add_argument("--http", type=int, default=wire.KH_PORT_HTTP)
    ap.add_argument("--udp", type=int, default=wire.KH_PORT_HUB, help="设备固件固定发往 47101，一般不要改")
    ap.add_argument("--tcp", type=int, default=wire.KH_PORT_TCP)
    ap.add_argument("--subnet", help="定向广播所在网段，例如 192.168.1.0/24（默认按本机地址的 /24）")
    ap.add_argument("--sweep", metavar="CIDR", help="每 15 s 逐个单播通知这个网段（路由器屏蔽广播时用）")
    ap.add_argument("--no-browser", action="store_true", help="不自动打开浏览器")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args(argv)
    sys.stdout.reconfigure(line_buffering=True)   # 在后台 / 重定向时也能及时看到启动信息

    store = Store(args.data)
    level = logging.DEBUG if args.verbose else logging.INFO
    handlers = [logging.StreamHandler(),
                logging.handlers.RotatingFileHandler(store.root / "hub.log", maxBytes=1 << 20, backupCount=3,
                                                     encoding="utf-8")]
    logging.basicConfig(level=level, format="%(asctime)s %(name)s %(levelname)s %(message)s", handlers=handlers)

    ip = lan_ip()
    core = HubCore(store, http_port=args.http, tcp_port=args.tcp)
    try:
        net = NetLoop(core, args.bind, args.udp, args.tcp, broadcast_addrs(ip, args.subnet), args.sweep)
        web = HubWeb(core, store, BOARD_HTML, HERE / "static", local_ips())
        server = web.make_server(args.bind, args.http)
    except OSError as exc:
        print(f"端口被占用或没有权限：{exc}。是不是已经有一个 hub 在运行？", file=sys.stderr)
        return 1
    threading.Thread(target=net.run, name="kj_hub_net", daemon=True).start()
    threading.Thread(target=server.serve_forever, name="kj_hub_web", daemon=True).start()

    board = f"http://127.0.0.1:{args.http}/board"
    print("限定猜拳电脑服务已启动")
    print(f"  看板（本机）：{board}")
    print(f"  服务首页（本机）：http://127.0.0.1:{args.http}/")
    print(f"  别的设备打开看板：http://{ip}:{args.http}/board?k={store.admin_token}  （只给庄家 / 主持人）")
    print(f"  局域网地址：{ip}  · 设备会自动找到这台电脑（UDP {args.udp}，TCP {args.tcp}）")
    print(f"  数据目录：{store.root}")
    print("  按 Ctrl+C 停止")
    if not args.no_browser:
        webbrowser.open(board)
    try:
        threading.Event().wait()
    except KeyboardInterrupt:
        print("\n正在停止…")
    net.stop.set()
    server.shutdown()
    return 0


if __name__ == "__main__":
    sys.exit(main())
