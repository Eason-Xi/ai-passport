#!/usr/bin/env python3
"""电脑 hub（tools/kj_hub）：线协议与 C 一侧的黄金向量一致、帧中继与扇出、发现与昵称规则、扫码登记、
看板行解析与记录、CSV 导出、网页访问控制、SSE，以及在 127.0.0.1 上的 UDP / TCP / HTTP 集成。只用标准库。"""

from __future__ import annotations

import json
import socket
import sys
import tempfile
import threading
import time
import unittest
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "kj_hub"))
sys.path.insert(0, str(ROOT / "tools"))

import kj_charset  # noqa: E402
import store as store_mod  # noqa: E402
import wire  # noqa: E402
from core import HubCore  # noqa: E402
from netio import NetLoop  # noqa: E402
from web import HubWeb  # noqa: E402

DEV_A = bytes.fromhex("246f28000001")
DEV_B = bytes.fromhex("246f28000002")
HOST = bytes.fromhex("246f2800a3f2")
NAME = "小明"   # 小明


class FakeClock:
    def __init__(self):
        self.t = 1000.0

    def __call__(self):
        return self.t


def env(kind, src, dst=wire.MAC_HUB, room=0, payload=b"", flags=0):
    return wire.Envelope(kind, src=src, dst=dst, room=room, payload=payload, flags=flags).encode()


def game_frame(ftype, room, body=b""):
    return bytes([ord("K"), ord("J"), wire.KJ_PROTO_VERSION, ftype, room & 0xFF, room >> 8]) + body


class HubTestCase(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.store = store_mod.Store(Path(self.tmp.name))
        self.clock = FakeClock()
        self.core = HubCore(self.store, http_port=8080, tcp_port=9000, clock=self.clock)

    def tearDown(self):
        self.tmp.cleanup()

    def send(self, data, addr):
        return self.core.on_datagram(data, addr)

    def discover(self, mac, addr, role=wire.KH_ROLE_PLAYER, room=0, name="", rev=0, proto=wire.KJ_PROTO_VERSION):
        d = wire.Discover(role=role, boot=7, fw="1.1.0", name=name, name_rev=rev, game_proto=proto)
        out = self.send(env(wire.KH_K_DISCOVER, mac, room=room, payload=d.encode()), addr)
        self.assertEqual(len(out), 1)
        data, to = out[0]
        self.assertEqual(to, addr)
        e = wire.Envelope.decode(data)
        self.assertEqual(e.kind, wire.KH_K_OFFER)
        self.assertEqual(e.dst, mac)
        return wire.Offer.decode(e.payload)

    def host_lines(self, cid=1, room="A3F2"):
        self.core.board_open(cid, "127.0.0.1:5000")
        self.core.board_line(cid, '@KJ {"t":"hello","app":"limited-rps","fw":"1.1.0","proto":2,"room":"%s",'
                                  '"mac":"%s","link":"wifi","max":128,"cards":4,"stars":3}' % (room, HOST.hex()))


class WireTest(unittest.TestCase):
    def test_golden_vectors_match_c(self):
        vectors = {}
        for line in (ROOT / "tests" / "data" / "kj_hub_vectors.txt").read_text(encoding="utf-8").splitlines():
            if line and not line.startswith("#"):
                name, hexstr = line.split()
                vectors[name] = bytes.fromhex(hexstr)
        src = bytes.fromhex("246f28aabbcc")
        hub_src = wire.MAC_HUB
        base = dict(room=0xA3F2, seq=0x0102, rssi=-60)
        built = {
            "frame": wire.Envelope(wire.KH_K_FRAME, src=src, dst=wire.MAC_BROADCAST, flags=wire.KH_FLAG_HOST,
                                   payload=b"KJ\x02\x01\xf2\xa3", **base),
            "discover": wire.Envelope(wire.KH_K_DISCOVER, src=src, payload=wire.Discover(
                role=wire.KH_ROLE_PLAYER, boot=0xBEEF, fw="1.1.0", name_rev=3, name=NAME).encode(), **base),
            "offer": wire.Envelope(wire.KH_K_OFFER, src=hub_src, dst=src, payload=wire.Offer(
                hub_id=0x12345678, roster_rev=7, flags=wire.KH_OFFER_NAME_VALID, name_rev=3, name=NAME).encode(),
                **base),
            "reg": wire.Envelope(wire.KH_K_REG, src=src, payload=wire.encode_reg("ABCDEFG2"), **base),
            "reg_state": wire.Envelope(wire.KH_K_REG_STATE, src=hub_src, dst=src, payload=wire.RegState(
                "ABCDEFG2", wire.KH_REG_DONE, 4, "Amy").encode(), **base),
            "name_get": wire.Envelope(wire.KH_K_NAME_GET, src=src, payload=wire.encode_name_get([3, 7, 128]), **base),
            "names": wire.Envelope(wire.KH_K_NAMES, src=hub_src, dst=src, payload=wire.Names(9, [
                wire.NameEntry(3, 0, NAME), wire.NameEntry(7, wire.KH_NAME_BOT | wire.KH_NAME_UNKNOWN)]).encode(),
                **base),
        }
        self.assertEqual(set(vectors), set(built))
        for name, e in built.items():
            self.assertEqual(e.encode().hex(), vectors[name].hex(), name)
            back = wire.Envelope.decode(vectors[name])
            self.assertEqual(back.kind, e.kind)
        self.assertEqual(wire.Discover.decode(wire.Envelope.decode(vectors["discover"]).payload).name, NAME)

    def test_rejects_and_sanitizes(self):
        good = env(wire.KH_K_FRAME, DEV_A, payload=b"x")
        for cut in range(len(good)):
            with self.assertRaises(wire.WireError):
                wire.Envelope.decode(good[:cut])
        with self.assertRaises(wire.WireError):
            wire.Envelope.decode(good + b"\0")
        with self.assertRaises(wire.WireError):
            wire.Envelope.decode(b"KJ" + good[2:])
        self.assertEqual(wire.sanitize_name("A\x01B\u0085C"), "ABC")
        self.assertEqual(wire.sanitize_name(b"x\xc3y"), "xy")
        self.assertEqual(wire.sanitize_name("一" * 9), "一" * 8)   # 截到 24 字节，不切半个字
        with self.assertRaises(wire.WireError):
            wire.encode_name_get([])
        with self.assertRaises(wire.WireError):
            wire.decode_name_get(b"\x01\x00")
        self.assertFalse(wire.token_valid("abcdefg2"))
        self.assertEqual(wire.frame_info(game_frame(1, 0xA3F2)), (1, 0xA3F2))
        self.assertIsNone(wire.frame_info(b"KJ\x01\x01\x00\x00"))


class CoreRelayTest(HubTestCase):
    def test_fanout_and_routing(self):
        a, b, h = ("10.0.0.2", 47102), ("10.0.0.3", 47102), ("10.0.0.9", 47102)
        self.discover(DEV_A, a)
        self.discover(DEV_B, b)
        self.discover(HOST, h, role=wire.KH_ROLE_HOST, room=0xA3F2)
        # 庄家的信标（广播）扇出给所有选手，不回给自己
        beacon = env(wire.KH_K_FRAME, HOST, wire.MAC_BROADCAST, 0xA3F2, game_frame(1, 0xA3F2), wire.KH_FLAG_HOST)
        out = self.send(beacon, h)
        self.assertEqual(sorted(to for _, to in out), sorted([a, b]))
        self.assertEqual(out[0][0], beacon)   # 原样转发
        # 选手的广播只到本赌局的庄家；单播按 MAC 转发
        out = self.send(env(wire.KH_K_FRAME, DEV_A, wire.MAC_BROADCAST, 0xA3F2, game_frame(2, 0xA3F2)), a)
        self.assertEqual([to for _, to in out], [h])
        out = self.send(env(wire.KH_K_FRAME, DEV_A, HOST, 0xA3F2, game_frame(3, 0xA3F2)), a)
        self.assertEqual([to for _, to in out], [h])
        # 目标不认识：丢弃并计数
        before = self.core.stats["dropped"]
        self.assertEqual(self.send(env(wire.KH_K_FRAME, DEV_A, bytes(6) + b"", 0, b"x")[:-1] + b"y", a), [])
        out = self.send(env(wire.KH_K_FRAME, DEV_A, bytes.fromhex("0102030405ff"), 0, game_frame(3, 1)), a)
        self.assertEqual(out, [])
        self.assertGreater(self.core.stats["dropped"], before)
        # 设备换了地址（DHCP 续租）：之后发往新地址
        a2 = ("10.0.0.22", 47102)
        self.send(env(wire.KH_K_FRAME, DEV_A, HOST, 0xA3F2, game_frame(3, 0xA3F2)), a2)
        out = self.send(env(wire.KH_K_FRAME, HOST, DEV_A, 0xA3F2, game_frame(4, 0xA3F2), wire.KH_FLAG_HOST), h)
        self.assertEqual([to for _, to in out], [a2])
        # 30 s 没动静的设备不再收到扇出
        self.clock.t += 31
        self.send(env(wire.KH_K_FRAME, DEV_B, HOST, 0xA3F2, game_frame(3, 0xA3F2)), b)
        out = self.send(beacon, h)
        self.assertEqual([to for _, to in out], [b])
        # 坏包计数
        self.assertEqual(self.send(b"garbage", a), [])
        self.assertGreaterEqual(self.core.stats["bad"], 1)


class CoreNameTest(HubTestCase):
    def test_discover_and_names(self):
        a = ("10.0.0.2", 47102)
        offer = self.discover(DEV_A, a)
        self.assertEqual(offer.hub_id, self.store.hub_id)
        self.assertEqual((offer.http_port, offer.tcp_port), (8080, 9000))
        self.assertTrue(offer.flags & wire.KH_OFFER_NAME_VALID)
        self.assertEqual(offer.name, "")
        # hub 没有记录、设备带着昵称来：补回登记表
        rev0 = self.core.roster_rev
        offer = self.discover(DEV_B, ("10.0.0.3", 47102), name=NAME, rev=5)
        self.assertEqual(offer.name, NAME)
        self.assertEqual(offer.name_rev, 5)
        self.assertEqual(self.store.registry.name_of(DEV_B.hex()), NAME)
        self.assertNotEqual(self.core.roster_rev, rev0)
        # hub 说了算：看板改名 / 清空后，设备再来时拿到新值
        ok, _ = self.core.rename(DEV_B.hex(), "Amy")
        self.assertTrue(ok)
        offer = self.discover(DEV_B, ("10.0.0.3", 47102), name=NAME, rev=5)
        self.assertEqual(offer.name, "Amy")
        self.assertEqual(offer.name_rev, 6)
        self.assertEqual(self.core.clear_names(), 1)
        offer = self.discover(DEV_B, ("10.0.0.3", 47102), name="Amy", rev=6)
        self.assertEqual(offer.name, "")
        self.assertEqual(offer.name_rev, 7)
        self.assertEqual(self.store.registry.name_of(DEV_B.hex()), "")
        # 旧固件：标记不兼容
        offer = self.discover(DEV_A, a, proto=1)
        self.assertTrue(offer.flags & wire.KH_OFFER_INCOMPATIBLE)
        # 广播的 OFFER 不带"对你有效"的昵称
        b = wire.Envelope.decode(self.core.broadcast_offer())
        self.assertEqual(b.dst, wire.MAC_BROADCAST)
        self.assertFalse(wire.Offer.decode(b.payload).flags & wire.KH_OFFER_NAME_VALID)

    def test_registration(self):
        a = ("10.0.0.2", 47102)
        self.discover(DEV_A, a)

        def reg(token, mac=DEV_A, addr=a):
            out = self.send(env(wire.KH_K_REG, mac, payload=wire.encode_reg(token)), addr)
            return wire.RegState.decode(wire.Envelope.decode(out[0][0]).payload)

        self.assertEqual(reg("ABCDEFG2").state, wire.KH_REG_WAITING)
        self.assertIsNone(self.core.reg_info("ZZZZZZZZ"))
        info = self.core.reg_info("ABCDEFG2")
        self.assertEqual(info["device"], "0001")
        self.assertEqual(reg("ABCDEFG2").state, wire.KH_REG_OPENED)
        # 别的设备拿着同一个 token：无效
        self.assertEqual(reg("ABCDEFG2", DEV_B, ("10.0.0.3", 47102)).state, wire.KH_REG_INVALID)
        # 校验：空、不支持的字、太长、控制字符、重名
        for bad, hint in [("", "请填写"), ("\U0001F600", "显示不了"), ("龘龘", "显示不了"),
                          ("一" * 9, "太长"), ("a" * 17, "太长"), ("A​B", "控制字符")]:
            ok, msg = self.core.reg_submit("ABCDEFG2", bad)
            self.assertFalse(ok, bad)
            self.assertIn(hint, msg)
        ok, msg = self.core.reg_submit("ABCDEFG2", "  小   明 ")   # 首尾空白去掉、连续空白合并
        self.assertTrue(ok, msg)
        self.assertEqual(msg, "小 明")
        st = reg("ABCDEFG2")
        self.assertEqual(st.state, wire.KH_REG_DONE)
        self.assertEqual(st.name, "小 明")
        self.assertEqual(st.name_rev, 1)
        self.discover(DEV_B, ("10.0.0.3", 47102))
        reg("BBBBBBB2", DEV_B, ("10.0.0.3", 47102))
        ok, msg = self.core.reg_submit("BBBBBBB2", "小 明")
        self.assertFalse(ok)
        self.assertIn("有人用了", msg)
        # 15 分钟后过期
        self.clock.t += 15 * 60 + 1
        self.assertIsNone(self.core.reg_info("BBBBBBB2"))
        ok, msg = self.core.reg_submit("BBBBBBB2", "Bob")
        self.assertFalse(ok)
        self.assertIn("过期", msg)
        # 与设备字库共用的字符集
        self.assertEqual(kj_charset.unsupported("张喆"), [])   # 张喆：补充字
        self.assertGreater(len(kj_charset.name_charset()), 6800)


class CoreBoardTest(HubTestCase):
    def test_board_lines_names_and_records(self):
        self.host_lines()
        self.assertEqual(self.core.board_open(2, "x"), [b"@KJ sync\n"])
        self.store.registry.set(DEV_A.hex(), NAME, "phone")
        self.core.board_line(1, '@KJ {"t":"g","room":"A3F2","phase":"run","gid":1,"seated":3,"online":3,"duels":0,'
                                '"cleared":0,"out":0,"failed":0,"bots":1,"cards":[12,12,12],"stars":9,"phase_ms":1,'
                                '"ms":1}')
        rev0 = self.core.roster_rev
        for no, mac, bot in [(1, DEV_A.hex(), 0), (2, DEV_B.hex(), 0), (3, "000000000000", 1)]:
            self.core.board_line(1, '@KJ {"t":"p","no":%d,"bot":%d,"on":1,"st":"idle","r":4,"s":4,"p":4,"stars":3,'
                                    '"peer":0,"lock":"","duel":0,"w":0,"l":0,"d":0,"fin":"","rssi":-50,'
                                    '"id":"%s","mac":"%s"}' % (no, bot, mac[-6:], mac))
        self.assertNotEqual(self.core.roster_rev, rev0)
        # NAME_GET：已登记的给昵称，没登记的 / 电脑选手带标志，不认识的座位算未知
        a = ("10.0.0.2", 47102)
        self.discover(DEV_A, a)
        out = self.send(env(wire.KH_K_NAME_GET, DEV_A, room=0xA3F2, payload=wire.encode_name_get([1, 2, 3, 9])), a)
        names = wire.Names.decode(wire.Envelope.decode(out[0][0]).payload)
        self.assertEqual(names.roster_rev, self.core.roster_rev)
        got = {e.no: (e.flags, e.name) for e in names.entries}
        self.assertEqual(got[1], (0, NAME))
        self.assertEqual(got[2], (wire.KH_NAME_UNKNOWN, ""))
        self.assertEqual(got[3], (wire.KH_NAME_BOT | wire.KH_NAME_UNKNOWN, ""))
        self.assertEqual(got[9], (wire.KH_NAME_UNKNOWN, ""))
        # 事件行补上昵称；记录写进 JSONL，导出 CSV 带 BOM 的中文表头由 web 层加
        self.core.board_line(1, '@KJ {"t":"e","k":"match","a":1,"b":2,"ca":"","cb":"","w":0,"duel":0,"x":42,"ms":5}')
        self.core.board_line(1, '@KJ {"t":"e","k":"rejoin","a":1,"b":0,"ca":"","cb":"","w":0,"duel":0,"x":0,"ms":6}')
        sessions = self.store.sessions.list()
        self.assertEqual(len(sessions), 1)
        self.assertEqual(sessions[0]["room"], "A3F2")
        rows = self.store.sessions.read(sessions[0]["file"])
        kinds = [r["t"] for r in rows]
        self.assertIn("g", kinds)
        self.assertEqual(kinds.count("p"), 3)
        ev = [r for r in rows if r["t"] == "e"]
        self.assertEqual(len(ev), 1)   # rejoin 不记
        self.assertEqual(ev[0]["a_name"], NAME)
        # 同样的 p 行（只有信号 / 在线变化）不重复记录
        self.core.board_line(1, '@KJ {"t":"p","no":1,"bot":0,"on":0,"st":"idle","r":4,"s":4,"p":4,"stars":3,'
                                '"peer":0,"lock":"","duel":0,"w":0,"l":0,"d":0,"fin":"","rssi":-70,'
                                '"id":"000001","mac":"%s"}' % DEV_A.hex())
        rows = self.store.sessions.read(sessions[0]["file"])
        self.assertEqual([r["t"] for r in rows].count("p"), 3)
        csv_text = store_mod.players_csv(rows)
        self.assertTrue(csv_text.startswith("赌局号,局号,编号,昵称"))
        self.assertIn(NAME, csv_text)
        self.assertIn("碰拳配对", store_mod.events_csv(rows))
        # 座位被移出：roster_rev 变化，NAME_GET 变成未知
        rev1 = self.core.roster_rev
        self.core.board_line(1, '@KJ {"t":"p","no":1,"gone":1}')
        self.assertNotEqual(self.core.roster_rev, rev1)
        # 看板命令：校验后写回这一赌局的连接
        self.assertEqual(self.core.board_command("A3F2", "start"), (True, ""))
        self.assertEqual(self.core.board_command("A3F2", "kick 12"), (True, ""))
        self.assertFalse(self.core.board_command("A3F2", "kick 0")[0])
        self.assertFalse(self.core.board_command("A3F2", "rm -rf")[0])
        self.assertFalse(self.core.board_command("FFFF", "start")[0])
        self.assertEqual(self.core.take_board_writes(), {1: [b"@KJ start\n", b"@KJ kick 12\n"]})
        self.assertEqual(self.core.take_board_writes(), {})
        # 连接断开：看板收到链路下线，命令无处可发
        self.core.board_close(1)
        self.assertFalse(self.core.board_command("A3F2", "start")[0])
        # 还不知道局号（没收到汇总行）时不记录：避免出现"第 0 局"的记录文件
        self.core.board_open(7, "x")
        self.core.board_line(7, '@KJ {"t":"hello","room":"B001","mac":"246f2800b001","proto":2}')
        self.core.board_line(7, '@KJ {"t":"p","no":1,"bot":0,"on":1,"st":"waiting","r":0,"s":0,"p":0,"stars":0,'
                                '"peer":0,"lock":"","duel":0,"w":0,"l":0,"d":0,"fin":"","rssi":-50,"id":"000001",'
                                '"mac":"%s"}' % DEV_A.hex())
        self.assertFalse(any(s["room"] == "B001" for s in self.store.sessions.list()))
        # 6 s 没有新行 / 12 s 没发身份行的连接视为失效
        self.host_lines(cid=5)
        self.assertFalse(self.core.board_stale(5))
        self.clock.t += 7
        self.assertTrue(self.core.board_stale(5))
        self.assertFalse(self.core.board_stale(2))
        self.clock.t += 6
        self.assertTrue(self.core.board_stale(2))

    def test_subscribe_snapshot_and_publish(self):
        self.host_lines()
        q = self.core.subscribe()
        first = q.get_nowait()
        self.assertEqual(first["t"], "hub")
        self.assertEqual(first["rooms"], ["A3F2"])
        items = [q.get_nowait() for _ in range(q.qsize())]
        self.assertTrue(any(i.get("t") == "hello" for i in items))
        self.core.board_line(1, '@KJ {"t":"ack","cmd":"start","arg":0,"ok":1,"err":""}')
        self.assertEqual(q.get_nowait()["t"], "ack")
        self.core.unsubscribe(q)


class LiveTest(HubTestCase):
    """在 127.0.0.1 的随机端口上跑真实的 UDP / TCP / HTTP。"""

    def setUp(self):
        super().setUp()
        self.core = HubCore(self.store)
        self.net = NetLoop(self.core, "127.0.0.1", 0, 0, broadcasts=["127.0.0.1"])
        self.core.tcp_port = self.net.tcp_port
        self.web = HubWeb(self.core, self.store, ROOT / "tools" / "kj_board" / "index.html",
                          ROOT / "tools" / "kj_hub" / "static", {"127.0.0.1"})
        self.server = self.web.make_server("127.0.0.1", 0)
        self.http = f"http://127.0.0.1:{self.server.server_address[1]}"
        threading.Thread(target=self.net.run, daemon=True).start()
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def tearDown(self):
        self.net.stop.set()
        self.server.shutdown()
        self.server.server_close()
        time.sleep(0.3)
        super().tearDown()

    def get(self, path, headers=None):
        req = urllib.request.Request(self.http + path, headers=headers or {})
        with urllib.request.urlopen(req, timeout=5) as r:
            return r.status, r.read().decode("utf-8"), r.headers

    def test_udp_tcp_http(self):
        # UDP：DISCOVER → OFFER
        dev = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        dev.bind(("127.0.0.1", 0))
        dev.settimeout(3)
        d = wire.Discover(role=wire.KH_ROLE_PLAYER, boot=1, fw="t")
        dev.sendto(env(wire.KH_K_DISCOVER, DEV_A, payload=d.encode()), ("127.0.0.1", self.net.udp_port))
        while True:
            data, _ = dev.recvfrom(1024)
            e = wire.Envelope.decode(data)
            if e.dst == DEV_A:
                break
        offer = wire.Offer.decode(e.payload)
        self.assertEqual(offer.tcp_port, self.net.tcp_port)
        # 登记：设备发 REG，手机打开网页、提交，设备收到完成
        dev.sendto(env(wire.KH_K_REG, DEV_A, payload=wire.encode_reg("QQQQQQQ2")), ("127.0.0.1", self.net.udp_port))
        time.sleep(0.3)
        status, page, headers = self.get("/j/QQQQQQQ2")
        self.assertEqual(status, 200)
        self.assertIn("0001", page)
        self.assertEqual(headers["Cache-Control"], "no-store")
        body = urllib.parse.urlencode({"name": "<b>小明</b>"}).encode()
        req = urllib.request.Request(self.http + "/j/QQQQQQQ2", data=body)
        with urllib.request.urlopen(req, timeout=5) as r:   # 带尖括号：设备字库没有 → 回到表单并转义回显
            page = r.read().decode("utf-8")
        self.assertIn("&lt;b&gt;", page)
        self.assertNotIn("<b>小", page)
        body = urllib.parse.urlencode({"name": NAME}).encode()
        with urllib.request.urlopen(urllib.request.Request(self.http + "/j/QQQQQQQ2", data=body), timeout=5) as r:
            self.assertTrue(r.url.endswith("/j/QQQQQQQ2/ok"))
            self.assertIn(NAME, r.read().decode("utf-8"))
        with self.assertRaises(urllib.error.HTTPError) as cm:
            self.get("/j/AAAAAAA2")
        self.assertEqual(cm.exception.code, 404)
        cm.exception.close()
        # TCP：庄家连上先收到 sync；发 hello 后看板命令能送达
        host = socket.create_connection(("127.0.0.1", self.net.tcp_port), timeout=3)
        self.assertEqual(host.recv(64).split(b"\n")[0], b"@KJ sync")
        host.sendall(b'@KJ {"t":"hello","room":"A3F2","mac":"246f2800a3f2","proto":2}\n')
        time.sleep(0.3)
        req = urllib.request.Request(self.http + "/api/cmd", data=json.dumps({"room": "A3F2", "cmd": "start"}).encode(),
                                     headers={"Content-Type": "application/json", "X-KJ-Admin": "1"})
        with urllib.request.urlopen(req, timeout=5) as r:
            self.assertTrue(json.loads(r.read())["ok"])
        got = b""
        deadline = time.time() + 3
        while b"@KJ start" not in got and time.time() < deadline:
            got += host.recv(256)
        self.assertIn(b"@KJ start\n", got)
        host.close()
        # 管理接口缺少自定义头：拒绝（防跨站表单）
        req = urllib.request.Request(self.http + "/api/cmd", data=b"{}", headers={"Content-Type": "application/json"})
        with self.assertRaises(urllib.error.HTTPError) as cm:
            urllib.request.urlopen(req, timeout=5)
        self.assertEqual(cm.exception.code, 400)
        cm.exception.close()
        # SSE：先推快照
        with urllib.request.urlopen(self.http + "/api/stream", timeout=5) as r:
            self.assertEqual(r.headers["Content-Type"], "text/event-stream; charset=utf-8")
            text = ""
            while "event: kj" not in text:
                text += r.readline().decode("utf-8")
            data = r.readline().decode("utf-8")
            self.assertTrue(data.startswith("data: "))
            self.assertEqual(json.loads(data[6:])["t"], "hub")
        dev.close()

    def test_access_control(self):
        status, page, _ = self.get("/board")
        self.assertEqual(status, 200)
        self.assertIn("限定猜拳", page)
        self.web.local_ips = {"10.9.9.9"}   # 假装请求来自别的设备
        for path in ("/board", "/", "/api/state", "/export/registry.csv"):
            with self.assertRaises(urllib.error.HTTPError) as cm:
                self.get(path)
            self.assertEqual(cm.exception.code, 403, path)
            cm.exception.close()
        status, _, headers = self.get("/board?k=" + self.store.admin_token)
        self.assertEqual(status, 200)
        self.assertIn("kj_admin=", headers["Set-Cookie"])
        self.assertIn("HttpOnly", headers["Set-Cookie"])
        status, text, _ = self.get("/api/state", {"Cookie": "kj_admin=" + self.store.admin_token})
        self.assertEqual(status, 200)
        with self.assertRaises(urllib.error.HTTPError) as cm:
            self.get("/board?k=wrong")
        cm.exception.close()
        self.assertEqual(self.get("/healthz")[1], "ok")   # 公开
        # CSV 导出：utf-8-sig
        self.web.local_ips = {"127.0.0.1"}
        self.store.registry.set(DEV_A.hex(), NAME, "phone")
        req = urllib.request.Request(self.http + "/export/registry.csv")
        with urllib.request.urlopen(req, timeout=5) as r:
            raw = r.read()
        self.assertTrue(raw.startswith(b"\xef\xbb\xbf"))
        self.assertIn(NAME, raw.decode("utf-8-sig"))


if __name__ == "__main__":
    unittest.main()
