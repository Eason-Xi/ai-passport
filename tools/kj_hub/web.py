"""hub 的网页：手机登记页（公开）、看板与管理接口（只允许本机或带令牌）、SSE、CSV 导出。只用标准库。"""

from __future__ import annotations

import hmac
import html
import json
import logging
import queue
import re
import time
from collections import deque
from http.cookies import SimpleCookie
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from string import Template
from urllib.parse import parse_qs, urlsplit

import store as store_mod
import wire
from core import HubCore

log = logging.getLogger("kj_hub.web")

BODY_MAX = 4096
SSE_PING_S = 15
REG_POST_PER_MIN = 10
TOKEN_RE = re.compile(r"/j/([A-Z2-7]{8})(/ok)?")
SESSION_RE = re.compile(r"/export/(\d{8}-\d{6}_[0-9A-F]{4}_g\d+\.jsonl)/(players|events)\.csv")


class HubWeb:
    def __init__(self, core: HubCore, store: store_mod.Store, board_html: Path, static_dir: Path,
                 local_ips: set[str]):
        self.core = core
        self.store = store
        self.board_html = board_html
        self.static_dir = static_dir
        self.local_ips = set(local_ips) | {"127.0.0.1", "::1"}
        self.rate: dict[str, deque] = {}

    def template(self, name: str) -> Template:
        return Template((self.static_dir / name).read_text(encoding="utf-8"))

    def allow_reg_post(self, ip: str) -> bool:
        now = time.monotonic()
        q = self.rate.setdefault(ip, deque())
        while q and now - q[0] > 60:
            q.popleft()
        if len(q) >= REG_POST_PER_MIN:
            return False
        q.append(now)
        return True

    def make_server(self, bind: str, port: int) -> ThreadingHTTPServer:
        web = self

        class Handler(_Handler):
            hub = web

        server = ThreadingHTTPServer((bind, port), Handler)
        server.daemon_threads = True
        return server


class _Handler(BaseHTTPRequestHandler):
    hub: HubWeb
    server_version = "kj-hub"
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):   # 不往终端刷访问日志
        log.debug("%s " + fmt, self.client_address[0], *args)

    # ------------------------------------------------------------ 工具
    def _send(self, code: int, body: bytes, ctype: str, extra: dict | None = None) -> None:
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")   # 微信内置浏览器缓存很激进
        self.send_header("X-Content-Type-Options", "nosniff")
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def _text(self, code: int, text: str, extra: dict | None = None) -> None:
        self._send(code, text.encode("utf-8"), "text/plain; charset=utf-8", extra)

    def _html(self, code: int, text: str, extra: dict | None = None) -> None:
        self._send(code, text.encode("utf-8"), "text/html; charset=utf-8", extra)

    def _json(self, code: int, obj, extra: dict | None = None) -> None:
        self._send(code, json.dumps(obj, ensure_ascii=False).encode("utf-8"), "application/json; charset=utf-8", extra)

    def _csv(self, name: str, text: str) -> None:
        # utf-8-sig：带 BOM，Excel / WPS 直接打开中文不乱码
        self._send(200, text.encode("utf-8-sig"), "text/csv; charset=utf-8",
                   {"Content-Disposition": f'attachment; filename="{name}"'})

    def _body(self) -> bytes | None:
        try:
            n = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            return None
        if n < 0 or n > BODY_MAX:
            return None
        return self.rfile.read(n) if n else b""

    def _token_from_request(self) -> str:
        q = parse_qs(urlsplit(self.path).query)
        if q.get("k"):
            return q["k"][0]
        if self.headers.get("X-KJ-Token"):
            return self.headers["X-KJ-Token"]
        cookie = SimpleCookie(self.headers.get("Cookie", ""))
        return cookie["kj_admin"].value if "kj_admin" in cookie else ""

    def _admin(self) -> tuple[bool, dict]:
        """看板与管理接口：本机直接放行；别的设备要带管理令牌（看板上能看到暗牌，不能让选手打开）。"""
        if self.client_address[0] in self.hub.local_ips:
            return True, {}
        token = self._token_from_request()
        if token and hmac.compare_digest(token, self.hub.store.admin_token):
            return True, {"Set-Cookie": f"kj_admin={token}; Path=/; HttpOnly; SameSite=Strict"}
        return False, {}

    def _deny(self) -> None:
        self._text(403, "只有运行 hub 的这台电脑能打开看板；别的设备请用 hub 终端里打印的带令牌的链接。")

    # ------------------------------------------------------------ GET
    def do_HEAD(self):
        self.do_GET()

    def do_GET(self):
        path = urlsplit(self.path).path
        m = TOKEN_RE.fullmatch(path)
        if m:
            return self._join_page(m.group(1), done=bool(m.group(2)))
        if path == "/healthz":
            return self._text(200, "ok")
        ok, cookie = self._admin()
        if not ok:
            return self._deny()
        if path in ("/", "/index.html"):
            return self._html(200, (self.hub.static_dir / "home.html").read_text(encoding="utf-8"), cookie)
        if path == "/board":
            return self._html(200, self.hub.board_html.read_text(encoding="utf-8"), cookie)
        if path == "/api/stream":
            return self._stream()
        if path == "/api/state":
            return self._json(200, {"devices": self.hub.core.devices_view(), "sessions": self.hub.store.sessions.list(),
                                    "rooms": [e["room"] for e in self.hub.core.snapshot() if e.get("t") == "link"],
                                    "stats": self.hub.core.stats, "admin_token": self.hub.store.admin_token})
        if path == "/export/registry.csv":
            return self._csv("registry.csv", self.hub.store.registry.csv())
        m = SESSION_RE.fullmatch(path)
        if m:
            try:
                rows = self.hub.store.sessions.read(m.group(1))
            except FileNotFoundError:
                return self._text(404, "没有这一局的记录")
            base = m.group(1)[:-len(".jsonl")]
            if m.group(2) == "players":
                return self._csv(f"{base}_players.csv", store_mod.players_csv(rows))
            return self._csv(f"{base}_events.csv", store_mod.events_csv(rows))
        return self._text(404, "not found")

    def _stream(self) -> None:
        q = self.hub.core.subscribe()
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Connection", "close")
        self.end_headers()
        try:
            self.wfile.write(b"retry: 2000\n\n")
            while True:
                try:
                    item = q.get(timeout=SSE_PING_S)
                except queue.Empty:
                    self.wfile.write(b": ping\n\n")
                    self.wfile.flush()
                    continue
                if item is None:   # 太慢被踢掉
                    break
                data = json.dumps(item, ensure_ascii=False)
                self.wfile.write(f"event: kj\ndata: {data}\n\n".encode("utf-8"))
                self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError, OSError):
            pass
        finally:
            self.hub.core.unsubscribe(q)
            self.close_connection = True

    def _join_page(self, token: str, done: bool, error: str = "", value: str = "") -> None:
        info = self.hub.core.reg_info(token)
        if info is None:
            return self._html(404, self.hub.template("expired.html").substitute())
        if done:
            return self._html(200, self.hub.template("done.html").substitute(name=html.escape(info["name"])))
        page = self.hub.template("join.html").substitute(
            device=html.escape(info["device"]), action=html.escape(f"/j/{token}"),
            value=html.escape(value or info["name"]), error=html.escape(error),
            error_style="" if error else "display:none", max_bytes=wire.KJ_NAME_MAX)
        return self._html(200, page)

    # ------------------------------------------------------------ POST
    def do_POST(self):
        path = urlsplit(self.path).path
        m = TOKEN_RE.fullmatch(path)
        if m and not m.group(2):
            return self._join_submit(m.group(1))
        ok, _ = self._admin()
        if not ok:
            return self._deny()
        # 管理接口：只收 JSON，并要求自定义请求头（别的网站的表单发不过来）
        if self.headers.get("X-KJ-Admin") != "1" or not self.headers.get("Content-Type", "").startswith(
                "application/json"):
            return self._json(400, {"ok": False, "err": "bad request"})
        body = self._body()
        try:
            req = json.loads(body or b"{}")
        except ValueError:
            req = None
        if not isinstance(req, dict):
            return self._json(400, {"ok": False, "err": "bad json"})
        if path == "/api/cmd":
            ok, err = self.hub.core.board_command(str(req.get("room", "")), str(req.get("cmd", "")))
            return self._json(200, {"ok": ok, "err": err})
        if path == "/api/name":
            ok, msg = self.hub.core.rename(str(req.get("mac", "")), str(req.get("name", "")))
            return self._json(200, {"ok": ok, "err": "" if ok else msg, "name": msg if ok else ""})
        if path == "/api/names/clear":
            return self._json(200, {"ok": True, "cleared": self.hub.core.clear_names()})
        return self._json(404, {"ok": False, "err": "not found"})

    def _join_submit(self, token: str) -> None:
        if not self.hub.allow_reg_post(self.client_address[0]):
            return self._join_page(token, False, "提交太频繁了，请稍等一分钟再试")
        body = self._body()
        if body is None:
            return self._join_page(token, False, "提交的内容太长了")
        name = parse_qs(body.decode("utf-8", "replace")).get("name", [""])[0]
        ok, msg = self.hub.core.reg_submit(token, name)
        if not ok:
            return self._join_page(token, False, msg, name)
        # 提交后跳到结果页（刷新不会重复提交）
        self.send_response(303)
        self.send_header("Location", f"/j/{token}/ok")
        self.send_header("Content-Length", "0")
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
