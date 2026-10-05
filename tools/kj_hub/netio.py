"""hub 的网络线程：一个 selectors 循环同时处理 UDP（设备中继 / 发现 / 登记 / 昵称）与庄家看板 TCP。只用标准库。"""

from __future__ import annotations

import ipaddress
import logging
import selectors
import socket
import threading
import time

import wire
from core import HubCore

log = logging.getLogger("kj_hub.net")

OFFER_EVERY_S = 2.0        # 周期广播 OFFER
SWEEP_EVERY_S = 15.0       # --sweep：逐个单播 OFFER 给本网段（路由器不转发广播时）
KEEPALIVE_EVERY_S = 2.0    # 给庄家看板连接发空行（设备 8 s 收不到任何东西就重连）
LINE_MAX = 1024


class BoardConn:
    def __init__(self, cid: int, sock: socket.socket, peer: str):
        self.cid = cid
        self.sock = sock
        self.peer = peer
        self.rbuf = b""
        self.wbuf = b""


class NetLoop:
    def __init__(self, core: HubCore, bind: str = "0.0.0.0", udp_port: int = wire.KH_PORT_HUB,
                 tcp_port: int = wire.KH_PORT_TCP, broadcasts: list[str] | None = None, sweep: str | None = None):
        self.core = core
        self.sel = selectors.DefaultSelector()
        self.udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.udp.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.udp.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        self.udp.bind((bind, udp_port))
        self.udp.setblocking(False)
        self.tcp = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.tcp.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.tcp.bind((bind, tcp_port))
        self.tcp.listen(8)
        self.tcp.setblocking(False)
        self.sel.register(self.udp, selectors.EVENT_READ, "udp")
        self.sel.register(self.tcp, selectors.EVENT_READ, "listen")
        self.conns: dict[int, BoardConn] = {}
        self.next_cid = 1
        self.broadcasts = broadcasts or ["255.255.255.255"]
        self.sweep_hosts = list(ipaddress.ip_network(sweep, strict=False).hosts())[:1024] if sweep else []
        self.stop = threading.Event()
        self.udp_port = self.udp.getsockname()[1]
        self.tcp_port = self.tcp.getsockname()[1]

    # -------------------------------------------------------------- UDP
    def _send(self, data: bytes, addr: tuple) -> None:
        try:
            self.udp.sendto(data, addr)
        except OSError as exc:   # 网络还没就绪 / 目标不可达：丢掉，协议层会重发
            log.debug("sendto %s failed: %s", addr, exc)

    def _udp_ready(self) -> None:
        for _ in range(64):
            try:
                data, addr = self.udp.recvfrom(wire.KH_DGRAM_MAX + 64)
            except (BlockingIOError, InterruptedError):
                return
            except OSError as exc:
                log.debug("recvfrom failed: %s", exc)
                return
            for out, to in self.core.on_datagram(data, addr):
                self._send(out, to)

    def _offer(self) -> None:
        data = self.core.broadcast_offer()
        for b in self.broadcasts:
            self._send(data, (b, wire.KH_PORT_DEVICE))

    def _sweep(self) -> None:
        data = self.core.broadcast_offer()
        for host in self.sweep_hosts:
            self._send(data, (str(host), wire.KH_PORT_DEVICE))

    # -------------------------------------------------------------- TCP
    def _accept(self) -> None:
        try:
            sock, peer = self.tcp.accept()
        except OSError:
            return
        sock.setblocking(False)
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        cid = self.next_cid
        self.next_cid += 1
        conn = BoardConn(cid, sock, f"{peer[0]}:{peer[1]}")
        self.conns[cid] = conn
        self.sel.register(sock, selectors.EVENT_READ, cid)
        for data in self.core.board_open(cid, conn.peer):
            conn.wbuf += data
        log.info("board connection %d from %s", cid, conn.peer)

    def _close(self, conn: BoardConn, why: str) -> None:
        log.info("board connection %d closed (%s)", conn.cid, why)
        try:
            self.sel.unregister(conn.sock)
        except (KeyError, ValueError):
            pass
        conn.sock.close()
        self.conns.pop(conn.cid, None)
        self.core.board_close(conn.cid)

    def _conn_read(self, conn: BoardConn) -> None:
        try:
            data = conn.sock.recv(4096)
        except (BlockingIOError, InterruptedError):
            return
        except OSError as exc:
            self._close(conn, str(exc))
            return
        if not data:
            self._close(conn, "eof")
            return
        conn.rbuf += data
        while b"\n" in conn.rbuf:
            line, conn.rbuf = conn.rbuf.split(b"\n", 1)
            if len(line) <= LINE_MAX:
                self.core.board_line(conn.cid, line.decode("utf-8", "replace"))
        if len(conn.rbuf) > LINE_MAX:
            conn.rbuf = b""   # 超长行：丢掉

    def _conn_write(self, conn: BoardConn) -> None:
        if not conn.wbuf:
            return
        try:
            n = conn.sock.send(conn.wbuf)
        except (BlockingIOError, InterruptedError):
            return
        except OSError as exc:
            self._close(conn, str(exc))
            return
        conn.wbuf = conn.wbuf[n:]

    def _update_interest(self) -> None:
        for cid, writes in self.core.take_board_writes().items():
            conn = self.conns.get(cid)
            if conn:
                conn.wbuf += b"".join(writes)
        for conn in list(self.conns.values()):
            events = selectors.EVENT_READ | (selectors.EVENT_WRITE if conn.wbuf else 0)
            try:
                self.sel.modify(conn.sock, events, conn.cid)
            except (KeyError, ValueError):
                pass

    # -------------------------------------------------------------- 主循环
    def run(self) -> None:
        last_offer = last_sweep = last_keepalive = 0.0
        while not self.stop.is_set():
            self._update_interest()
            for key, mask in self.sel.select(timeout=0.2):
                if key.data == "udp":
                    self._udp_ready()
                elif key.data == "listen":
                    self._accept()
                else:
                    conn = self.conns.get(key.data)
                    if not conn:
                        continue
                    if mask & selectors.EVENT_READ:
                        self._conn_read(conn)
                    if conn.cid in self.conns and mask & selectors.EVENT_WRITE:
                        self._conn_write(conn)
            now = time.monotonic()
            if now - last_offer >= OFFER_EVERY_S:
                last_offer = now
                self._offer()
            if self.sweep_hosts and now - last_sweep >= SWEEP_EVERY_S:
                last_sweep = now
                self._sweep()
            if now - last_keepalive >= KEEPALIVE_EVERY_S:
                last_keepalive = now
                for conn in list(self.conns.values()):
                    if self.core.board_stale(conn.cid):
                        self._close(conn, "no lines from host")
                    else:
                        conn.wbuf += b"\n"
        for conn in list(self.conns.values()):
            self._close(conn, "hub stopping")
        self.sel.close()
        self.udp.close()
        self.tcp.close()
