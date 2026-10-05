"""hub 的核心逻辑：设备表与帧中继、发现 / 保活、昵称登记与查询、庄家看板行的解析与记录、SSE 订阅。

纯逻辑：不碰 socket，时钟可注入（便于测试）。网络线程与 HTTP 线程都在 self.lock 里调用这里的方法。
"""

from __future__ import annotations

import json
import queue
from collections import deque
import re
import sys
import threading
import time
import unicodedata
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # tools/：与设备共用的昵称字符集
import kj_charset  # noqa: E402
import wire  # noqa: E402
from store import Store  # noqa: E402

DEVICE_TTL_S = 30          # 多久没收到某台设备的数据报就不再给它扇出信标
REG_TTL_S = 15 * 60        # 登记 token 的有效期
REG_MAX = 256              # 同时等待登记的 token 上限
BOARD_STALE_S = 6          # 庄家看板连接多久没有新行就断开（庄家每秒发一行汇总）
BOARD_HELLO_S = 12         # 连上后这么久还没发身份行（hello）也断开
SSE_QUEUE_MAX = 500
RECENT_EVENTS = 60         # 新打开的看板补发最近这么多条对局事件
LOG_GAME_EVERY_S = 60      # 汇总行（g）在阶段不变时多久记一次
COMMANDS = ("start", "end", "new", "reset", "bot+", "bot-", "sync")   # 另有 "kick N"
VOLATILE_P = ("on", "rssi")


def room_text(room: int) -> str:
    return f"{room & 0xFFFF:04X}"


@dataclass
class Device:
    mac: bytes
    addr: tuple
    last_seen: float
    role: int = wire.KH_ROLE_NONE
    room: int = 0
    boot: int = 0
    fw: str = ""
    rssi: int = 0
    proto_ok: bool = True

    @property
    def is_host(self) -> bool:
        return self.role == wire.KH_ROLE_HOST


@dataclass
class RoomState:
    room: int
    host_mac: str = ""
    conn: int | None = None
    hello: dict = field(default_factory=dict)
    game: dict = field(default_factory=dict)
    seats: dict = field(default_factory=dict)       # no -> 最新的 p 行
    gid: int = 0
    last_line: float = 0.0
    last_logged_game: float = 0.0
    last_logged_phase: str = ""
    logged_p: dict = field(default_factory=dict)    # no -> 上次写进记录的 p（去掉易变字段）
    recent: deque = field(default_factory=lambda: deque(maxlen=RECENT_EVENTS))


@dataclass
class Registration:
    token: str
    mac: str
    created: float
    state: int = wire.KH_REG_WAITING


class HubCore:
    def __init__(self, store: Store, http_port: int = wire.KH_PORT_HTTP, tcp_port: int = wire.KH_PORT_TCP,
                 clock=time.monotonic):
        self.store = store
        self.http_port = http_port
        self.tcp_port = tcp_port
        self.clock = clock
        self.lock = threading.RLock()
        self.devices: dict[bytes, Device] = {}
        self.rooms: dict[int, RoomState] = {}
        self.conn_room: dict[int, int] = {}               # 看板连接 → 赌局号
        self.conn_peer: dict[int, str] = {}
        self.conn_opened: dict[int, float] = {}
        self.board_writes: dict[int, list[bytes]] = {}
        self.regs: dict[str, Registration] = {}
        self.roster_rev = 1
        self.subscribers: list[queue.Queue] = []
        self.stats = {"relayed": 0, "dropped": 0, "bad": 0, "discover": 0}
        self.seq = 0

    # ------------------------------------------------------------------
    # 设备 → hub 的数据报
    # ------------------------------------------------------------------

    def on_datagram(self, data: bytes, addr: tuple) -> list[tuple[bytes, tuple]]:
        with self.lock:
            try:
                env = wire.Envelope.decode(data)
            except wire.WireError:
                self.stats["bad"] += 1
                return []
            now = self.clock()
            dev = self.devices.get(env.src)
            if dev is None:
                dev = self.devices[env.src] = Device(env.src, addr, now)
            dev.addr = addr
            dev.last_seen = now
            dev.rssi = env.rssi
            if env.flags & wire.KH_FLAG_HOST:
                dev.role = wire.KH_ROLE_HOST
            handler = {
                wire.KH_K_FRAME: self._on_frame,
                wire.KH_K_DISCOVER: self._on_discover,
                wire.KH_K_REG: self._on_reg,
                wire.KH_K_NAME_GET: self._on_name_get,
            }.get(env.kind)
            if handler is None:
                return []
            try:
                return handler(env, dev, now)
            except wire.WireError:
                self.stats["bad"] += 1
                return []

    def _reply(self, kind: int, dst: Device, room: int, payload: bytes) -> tuple[bytes, tuple]:
        self.seq = (self.seq + 1) & 0xFFFF
        env = wire.Envelope(kind, src=self._hub_mac(), dst=dst.mac, room=room, seq=self.seq, payload=payload)
        return env.encode(), dst.addr

    def _hub_mac(self) -> bytes:
        return b"\x00\x00" + (self.store.hub_id & 0xFFFFFFFF).to_bytes(4, "big")

    def _alive(self, dev: Device, now: float) -> bool:
        return now - dev.last_seen < DEVICE_TTL_S

    def _on_frame(self, env: wire.Envelope, dev: Device, now: float) -> list[tuple[bytes, tuple]]:
        info = wire.frame_info(env.payload)
        if info and info[0] == wire.KJ_F_ROOM and dev.is_host:
            room = self.rooms.setdefault(info[1], RoomState(info[1]))
            room.host_mac = dev.mac.hex()
            dev.room = info[1]
        data = env.encode()   # 原样转发（保留源 MAC 与发送方信号）
        out = []
        if env.dst == wire.MAC_BROADCAST:
            if dev.is_host:   # 庄家的信标：扇出给所有选手设备
                targets = [d for d in self.devices.values()
                           if d is not dev and not d.is_host and self._alive(d, now)]
            else:             # 选手的广播只给本赌局的庄家
                room = self.rooms.get(env.room)
                host = self.devices.get(bytes.fromhex(room.host_mac)) if room and room.host_mac else None
                targets = [host] if host and self._alive(host, now) else []
        else:
            target = self.devices.get(env.dst)
            targets = [target] if target else []
        if not targets:
            self.stats["dropped"] += 1
        for t in targets:
            out.append((data, t.addr))
        self.stats["relayed"] += len(out)
        return out

    def _on_discover(self, env: wire.Envelope, dev: Device, now: float) -> list[tuple[bytes, tuple]]:
        d = wire.Discover.decode(env.payload)
        self.stats["discover"] += 1
        dev.role = d.role
        dev.room = env.room
        dev.boot = d.boot
        dev.fw = d.fw
        dev.proto_ok = d.hub_proto == wire.KH_VERSION and d.game_proto == wire.KJ_PROTO_VERSION
        mac = dev.mac.hex()
        entry = self.store.registry.get(mac)
        if entry is None and d.name:
            name = self._clean(d.name)
            if name and not self._name_problem(name, mac):
                entry = self.store.registry.adopt(mac, name, d.name_rev)
                self._roster_changed()
        offer = wire.Offer(hub_id=self.store.hub_id, http_port=self.http_port, tcp_port=self.tcp_port,
                           roster_rev=self.roster_rev, flags=wire.KH_OFFER_NAME_VALID)
        if entry:
            offer.name = "" if entry.get("deleted") else entry["name"]
            offer.name_rev = entry["rev"]
        if not dev.proto_ok:
            offer.flags |= wire.KH_OFFER_INCOMPATIBLE
        return [self._reply(wire.KH_K_OFFER, dev, env.room, offer.encode())]

    def broadcast_offer(self) -> bytes:
        """周期广播的 OFFER（不带"对你有效"的昵称）。"""
        with self.lock:
            offer = wire.Offer(hub_id=self.store.hub_id, http_port=self.http_port, tcp_port=self.tcp_port,
                               roster_rev=self.roster_rev)
            self.seq = (self.seq + 1) & 0xFFFF
            return wire.Envelope(wire.KH_K_OFFER, src=self._hub_mac(), dst=wire.MAC_BROADCAST, seq=self.seq,
                                 payload=offer.encode()).encode()

    def _on_reg(self, env: wire.Envelope, dev: Device, now: float) -> list[tuple[bytes, tuple]]:
        token = wire.decode_reg(env.payload)
        self._expire_regs(now)
        mac = dev.mac.hex()
        reg = self.regs.get(token)
        if reg is None and len(self.regs) < REG_MAX:
            reg = self.regs[token] = Registration(token, mac, now)
        msg = wire.RegState(token, wire.KH_REG_INVALID)
        if reg and reg.mac == mac:
            msg.state = reg.state
            if reg.state == wire.KH_REG_DONE:
                entry = self.store.registry.get(mac)
                if entry:
                    msg.name = "" if entry.get("deleted") else entry["name"]
                    msg.name_rev = entry["rev"]
        return [self._reply(wire.KH_K_REG_STATE, dev, env.room, msg.encode())]

    def _on_name_get(self, env: wire.Envelope, dev: Device, now: float) -> list[tuple[bytes, tuple]]:
        nos = wire.decode_name_get(env.payload)
        room = self.rooms.get(env.room)
        names = wire.Names(self.roster_rev)
        for no in nos:
            entry = wire.NameEntry(no, wire.KH_NAME_UNKNOWN)
            seat = room.seats.get(no) if room else None
            if seat and seat.get("bot"):
                entry.flags = wire.KH_NAME_BOT | wire.KH_NAME_UNKNOWN
            elif seat and seat.get("mac"):
                name = self.store.registry.name_of(seat["mac"])
                if name:
                    entry.flags = 0
                    entry.name = name
            names.entries.append(entry)
        return [self._reply(wire.KH_K_NAMES, dev, env.room, names.encode())]

    def _roster_changed(self) -> None:
        self.roster_rev = (self.roster_rev + 1) & 0xFFFFFFFF or 1

    # ------------------------------------------------------------------
    # 登记网页与改名
    # ------------------------------------------------------------------

    def _expire_regs(self, now: float) -> None:
        for token in [t for t, r in self.regs.items() if now - r.created > REG_TTL_S]:
            del self.regs[token]

    def _clean(self, raw: str) -> str:
        name = unicodedata.normalize("NFC", raw or "")
        return re.sub(r"\s+", " ", name).strip()

    def _name_problem(self, name: str, mac: str) -> str | None:
        """昵称不合格时返回给用户看的原因（字符集与设备的昵称字库共用 tools/kj_charset.py）。"""
        if not name:
            return "请填写昵称"
        if any(unicodedata.category(c) in ("Cc", "Cf") for c in name):
            return "昵称里有看不见的控制字符"
        bad = kj_charset.unsupported(name)
        if bad:
            return "设备显示不了这些字：" + " ".join(bad) + "，换一个吧"
        if len(name.encode("utf-8")) > wire.KJ_NAME_MAX or kj_charset.display_units(name) > kj_charset.NAME_MAX_UNITS:
            return "昵称太长了：最多 8 个汉字或 16 个字母"
        owner = self.store.registry.owner_of(name)
        if owner and owner != mac:
            return "这个昵称已经有人用了，换一个吧"
        return None

    def reg_info(self, token: str) -> dict | None:
        """手机打开登记页：标记"已扫码"，返回页面需要的信息；token 无效返回 None。"""
        with self.lock:
            self._expire_regs(self.clock())
            reg = self.regs.get(token)
            if not reg:
                return None
            if reg.state == wire.KH_REG_WAITING:
                reg.state = wire.KH_REG_OPENED
            return {"device": reg.mac[-4:].upper(), "name": self.store.registry.name_of(reg.mac),
                    "done": reg.state == wire.KH_REG_DONE}

    def reg_submit(self, token: str, raw: str) -> tuple[bool, str]:
        with self.lock:
            self._expire_regs(self.clock())
            reg = self.regs.get(token)
            if not reg:
                return False, "二维码已过期，请在设备上重新打开登记页"
            ok, msg = self._set_name(reg.mac, raw, "phone")
            if ok:
                reg.state = wire.KH_REG_DONE
            return ok, msg

    def rename(self, mac: str, raw: str) -> tuple[bool, str]:
        with self.lock:
            if not re.fullmatch(r"[0-9a-f]{12}", mac or ""):
                return False, "设备不对"
            if not (raw or "").strip():
                self.store.registry.delete(mac, "board")
                self._roster_changed()
                self._publish_names()
                return True, ""
            return self._set_name(mac, raw, "board")

    def _set_name(self, mac: str, raw: str, source: str) -> tuple[bool, str]:
        name = self._clean(raw)
        problem = self._name_problem(name, mac)
        if problem:
            return False, problem
        if self.store.registry.name_of(mac) != name:
            self.store.registry.set(mac, name, source)
            self._roster_changed()
            self._publish_names()
        return True, name

    def clear_names(self) -> int:
        with self.lock:
            n = self.store.registry.clear_all("board")
            if n:
                self._roster_changed()
                self._publish_names()
            return n

    def _publish_names(self) -> None:
        """昵称变了：把每个座位的最新 p 行（带新昵称）重新推给看板。"""
        for room in self.rooms.values():
            for seat in room.seats.values():
                self._publish(dict(seat, room=room_text(room.room)))

    # ------------------------------------------------------------------
    # 庄家看板（TCP 行）
    # ------------------------------------------------------------------

    def board_open(self, cid: int, peer: str) -> list[bytes]:
        with self.lock:
            self.conn_peer[cid] = peer
            self.conn_opened[cid] = self.clock()
            self.board_writes.setdefault(cid, [])
            return [b"@KJ sync\n"]

    def board_close(self, cid: int) -> None:
        with self.lock:
            room_id = self.conn_room.pop(cid, None)
            self.conn_peer.pop(cid, None)
            self.conn_opened.pop(cid, None)
            self.board_writes.pop(cid, None)
            room = self.rooms.get(room_id) if room_id is not None else None
            if room and room.conn == cid:
                room.conn = None
                self._publish({"t": "link", "room": room_text(room.room), "up": 0})

    def board_stale(self, cid: int) -> bool:
        with self.lock:
            now = self.clock()
            room_id = self.conn_room.get(cid)
            room = self.rooms.get(room_id) if room_id is not None else None
            if room is None:
                return now - self.conn_opened.get(cid, now) > BOARD_HELLO_S
            return now - room.last_line > BOARD_STALE_S

    def board_line(self, cid: int, line: str) -> None:
        with self.lock:
            line = line.strip()
            if not line.startswith("@KJ {"):
                return
            try:
                obj = json.loads(line[4:])
            except ValueError:
                return
            if not isinstance(obj, dict):
                return
            now = self.clock()
            if obj.get("t") == "hello":
                try:
                    room_id = int(str(obj.get("room", "")), 16)
                except ValueError:
                    return
                room = self.rooms.setdefault(room_id, RoomState(room_id))
                if room.conn not in (None, cid):
                    self.conn_room.pop(room.conn, None)   # 同一赌局换了连接（庄家重启）：以新的为准
                room.conn = cid
                room.hello = obj
                room.host_mac = str(obj.get("mac", room.host_mac))
                self.conn_room[cid] = room_id
                room.last_line = now
                self._publish(dict(obj, room=room_text(room_id)))
                self._publish({"t": "link", "room": room_text(room_id), "up": 1})
                return
            room_id = self.conn_room.get(cid)
            if room_id is None:
                return   # 还没收到身份行：等庄家补发
            room = self.rooms[room_id]
            room.last_line = now
            rt = room_text(room_id)
            kind = obj.get("t")
            if kind == "g":
                gid = int(obj.get("gid", 0) or 0)
                if gid != room.gid:
                    room.gid = gid
                    room.logged_p.clear()
                room.game = obj
                if obj.get("phase") != room.last_logged_phase or now - room.last_logged_game >= LOG_GAME_EVERY_S:
                    room.last_logged_phase = obj.get("phase", "")
                    room.last_logged_game = now
                    self.store.sessions.write(rt, room.gid, obj)
            elif kind == "p":
                no = obj.get("no")
                if not isinstance(no, int):
                    return
                if obj.get("gone"):
                    if room.seats.pop(no, None) is not None:
                        self._roster_changed()
                    room.logged_p.pop(no, None)
                else:
                    old = room.seats.get(no)
                    if not old or old.get("mac") != obj.get("mac"):
                        self._roster_changed()
                    obj["name"] = "" if obj.get("bot") else self.store.registry.name_of(str(obj.get("mac", "")))
                    room.seats[no] = obj
                stable = {k: v for k, v in obj.items() if k not in VOLATILE_P}
                if room.gid and room.logged_p.get(no) != stable:   # 收到第一行汇总（知道局号）之后才开始记录
                    room.logged_p[no] = stable
                    self.store.sessions.write(rt, room.gid, stable)
            elif kind == "e":
                for side in ("a", "b"):
                    seat = room.seats.get(obj.get(side))
                    if seat and seat.get("name"):
                        obj[side + "_name"] = seat["name"]
                if obj.get("k") != "rejoin":
                    if room.gid:
                        self.store.sessions.write(rt, room.gid, obj)
                    room.recent.append(obj)
                if obj.get("k") in ("reset", "new"):
                    room.recent.clear()
            elif kind == "ack":
                pass
            else:
                return
            self._publish(dict(obj, room=rt))

    def board_command(self, room_txt: str, cmd: str) -> tuple[bool, str]:
        with self.lock:
            cmd = (cmd or "").strip()
            m = re.fullmatch(r"kick (\d{1,3})", cmd)
            if cmd not in COMMANDS and not (m and 1 <= int(m.group(1)) <= 128):
                return False, "命令不对"
            try:
                room = self.rooms.get(int(room_txt, 16))
            except (TypeError, ValueError):
                room = None
            if not room or room.conn is None:
                return False, "这个赌局的庄家没有连上"
            self.board_writes.setdefault(room.conn, []).append(f"@KJ {cmd}\n".encode())
            return True, ""

    def take_board_writes(self) -> dict[int, list[bytes]]:
        with self.lock:
            out = {cid: w for cid, w in self.board_writes.items() if w}
            for cid in out:
                self.board_writes[cid] = []
            return out

    # ------------------------------------------------------------------
    # 浏览器（SSE）
    # ------------------------------------------------------------------

    def subscribe(self) -> queue.Queue:
        with self.lock:
            q: queue.Queue = queue.Queue(maxsize=SSE_QUEUE_MAX)
            for item in self.snapshot():
                q.put_nowait(item)
            self.subscribers.append(q)
            return q

    def unsubscribe(self, q: queue.Queue) -> None:
        with self.lock:
            if q in self.subscribers:
                self.subscribers.remove(q)

    def _publish(self, obj: dict) -> None:
        dead = []
        for q in self.subscribers:
            try:
                q.put_nowait(obj)
            except queue.Full:
                dead.append(q)   # 浏览器太慢：断开，它会自动重连并拿到快照
        for q in dead:
            self.subscribers.remove(q)
            try:
                q.put_nowait(None)
            except queue.Full:
                pass

    def snapshot(self) -> list[dict]:
        with self.lock:
            out = [{"t": "hub", "rooms": [room_text(r) for r in sorted(self.rooms)]}]
            for room in self.rooms.values():
                rt = room_text(room.room)
                if room.hello:
                    out.append(dict(room.hello, room=rt))
                out.append({"t": "link", "room": rt, "up": 1 if room.conn is not None else 0})
                if room.game:
                    out.append(dict(room.game, room=rt))
                for seat in room.seats.values():
                    out.append(dict(seat, room=rt))
                for ev in room.recent:   # 最近的对局事件：看板的"对局记录"不是空的
                    out.append(dict(ev, room=rt))
            return out

    def devices_view(self) -> list[dict]:
        with self.lock:
            now = self.clock()
            out = []
            for d in sorted(self.devices.values(), key=lambda d: d.mac):
                mac = d.mac.hex()
                out.append({"mac": mac, "id": mac[-4:].upper(), "ip": d.addr[0], "role": d.role,
                            "room": room_text(d.room) if d.room else "", "fw": d.fw, "rssi": d.rssi,
                            "online": now - d.last_seen < DEVICE_TTL_S, "seen_s": round(now - d.last_seen, 1),
                            "name": self.store.registry.name_of(mac), "proto_ok": d.proto_ok})
            return out
