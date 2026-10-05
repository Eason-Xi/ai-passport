"""设备 ⇄ hub 的 UDP 线协议（与 main/kj_hubproto.h 一一对应，只用标准库）。

信封 24 字节（小端）：'K' 'H' ver kind src[6] dst[6] room seq rssi flags len，后接 len 字节载荷。
两边的常量由 tests/test_kj_contract.py 对照，编码由 tests/data/kj_hub_vectors.txt 的黄金向量逐字节对照。
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field

KH_VERSION = 1
KH_HDR = 24
KH_PAYLOAD_MAX = 480
KH_DGRAM_MAX = KH_HDR + KH_PAYLOAD_MAX

KH_PORT_HUB = 47101
KH_PORT_DEVICE = 47102
KH_PORT_TCP = 47103
KH_PORT_HTTP = 47180

KJ_NAME_MAX = 24
KJ_REG_TOKEN_LEN = 8
KH_FW_LEN = 12
KH_NAMES_MAX = 6

KH_FLAG_HOST = 0x01

KH_K_FRAME = 1
KH_K_DISCOVER = 2
KH_K_OFFER = 3
KH_K_REG = 4
KH_K_REG_STATE = 5
KH_K_NAME_GET = 6
KH_K_NAMES = 7
KH_K_COUNT = 8

KH_ROLE_NONE = 0
KH_ROLE_PLAYER = 1
KH_ROLE_HOST = 2

KH_OFFER_NAME_VALID = 0x01
KH_OFFER_INCOMPATIBLE = 0x02

KH_REG_INVALID = 0
KH_REG_WAITING = 1
KH_REG_OPENED = 2
KH_REG_DONE = 3

KH_NAME_BOT = 0x01
KH_NAME_UNKNOWN = 0x02

# 游戏帧（main/kj_proto.h）里 hub 需要认识的部分
KJ_PROTO_VERSION = 2
KJ_FRAME_HEADER = 6
KJ_FRAME_MAX = 64
KJ_F_ROOM = 1

MAC_BROADCAST = b"\xff" * 6
MAC_HUB = b"\x00" * 6

TOKEN_ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567"

_ENV = struct.Struct("<2sBB6s6sHHbBH")
assert _ENV.size == KH_HDR


class WireError(ValueError):
    """畸形或不认识的数据报。"""


@dataclass
class Envelope:
    kind: int
    src: bytes = MAC_HUB
    dst: bytes = MAC_HUB
    room: int = 0
    seq: int = 0
    rssi: int = 0
    flags: int = 0
    payload: bytes = b""

    def encode(self) -> bytes:
        if not 0 < self.kind < KH_K_COUNT or len(self.payload) > KH_PAYLOAD_MAX:
            raise WireError("bad envelope")
        return _ENV.pack(b"KH", KH_VERSION, self.kind, self.src, self.dst, self.room & 0xFFFF,
                         self.seq & 0xFFFF, self.rssi, self.flags, len(self.payload)) + self.payload

    @classmethod
    def decode(cls, data: bytes) -> "Envelope":
        if len(data) < KH_HDR:
            raise WireError("short")
        magic, ver, kind, src, dst, room, seq, rssi, flags, length = _ENV.unpack_from(data)
        if magic != b"KH" or ver != KH_VERSION:
            raise WireError("magic/version")
        if not 0 < kind < KH_K_COUNT:
            raise WireError("kind")
        if length > KH_PAYLOAD_MAX or KH_HDR + length != len(data):
            raise WireError("length")
        return cls(kind, src, dst, room, seq, rssi, flags, bytes(data[KH_HDR:]))


# ---------------------------------------------------------------------------
# 名字：len u8 + UTF-8；写入前清理并按字符边界截断，读出后清理（与 kj_utf8_sanitize 一致）
# ---------------------------------------------------------------------------

def _is_control(cp: int) -> bool:
    return cp < 0x20 or cp == 0x7F or 0x80 <= cp < 0xA0


def sanitize_name(raw: bytes | str, max_bytes: int = KJ_NAME_MAX) -> str:
    """丢掉非法 UTF-8 与控制字符，截到 max_bytes 字节以内（不切开字符）。"""
    if isinstance(raw, str):
        raw = raw.encode("utf-8", "surrogatepass")
    out = bytearray()
    i = 0
    while i < len(raw):
        k = _utf8_len(raw, i)
        if k == 0:
            i += 1
            continue
        ch = raw[i:i + k]
        cp = ord(ch.decode("utf-8"))
        if not _is_control(cp):
            if len(out) + k > max_bytes:
                break
            out += ch
        i += k
    return out.decode("utf-8")


def _utf8_len(b: bytes, i: int) -> int:
    """从 i 开始的一个合法 UTF-8 字符的字节数；不合法返回 0（规则同 kj_utf8_decode）。"""
    c = b[i]
    if c < 0x80:
        return 1
    if c & 0xE0 == 0xC0:
        n, lo = 2, 0x80
    elif c & 0xF0 == 0xE0:
        n, lo = 3, 0x800
    elif c & 0xF8 == 0xF0:
        n, lo = 4, 0x10000
    else:
        return 0
    if i + n > len(b):
        return 0
    cp = c & (0x7F >> n)
    for j in range(1, n):
        if b[i + j] & 0xC0 != 0x80:
            return 0
        cp = (cp << 6) | (b[i + j] & 0x3F)
    if cp < lo or cp > 0x10FFFF or 0xD800 <= cp <= 0xDFFF:
        return 0
    return n


def _w_name(name: str) -> bytes:
    data = sanitize_name(name).encode("utf-8")
    return bytes([len(data)]) + data


class _Reader:
    def __init__(self, data: bytes):
        self.data = data
        self.pos = 0

    def take(self, n: int) -> bytes:
        if self.pos + n > len(self.data):
            raise WireError("truncated")
        out = self.data[self.pos:self.pos + n]
        self.pos += n
        return out

    def u8(self) -> int:
        return self.take(1)[0]

    def u16(self) -> int:
        return struct.unpack("<H", self.take(2))[0]

    def u32(self) -> int:
        return struct.unpack("<I", self.take(4))[0]

    def name(self) -> str:
        n = self.u8()
        if n > KJ_NAME_MAX:
            raise WireError("name too long")
        return sanitize_name(self.take(n))


# ---------------------------------------------------------------------------
# 控制消息
# ---------------------------------------------------------------------------

@dataclass
class Discover:
    hub_proto: int = KH_VERSION
    game_proto: int = KJ_PROTO_VERSION
    role: int = KH_ROLE_NONE
    boot: int = 0
    fw: str = ""
    name_rev: int = 0
    name: str = ""

    def encode(self) -> bytes:
        fw = self.fw.encode("ascii", "replace")[:KH_FW_LEN].ljust(KH_FW_LEN, b"\0")
        return (struct.pack("<BBBH", self.hub_proto, self.game_proto, self.role, self.boot) + fw +
                struct.pack("<I", self.name_rev) + _w_name(self.name))

    @classmethod
    def decode(cls, data: bytes) -> "Discover":
        r = _Reader(data)
        m = cls(r.u8(), r.u8(), r.u8(), r.u16())
        raw = r.take(KH_FW_LEN).split(b"\0", 1)[0]
        m.fw = "".join(chr(b) if 0x20 <= b < 0x7F else "?" for b in raw)
        m.name_rev = r.u32()
        m.name = r.name()
        if m.role > KH_ROLE_HOST:
            raise WireError("role")
        return m


@dataclass
class Offer:
    hub_id: int = 0
    http_port: int = KH_PORT_HTTP
    tcp_port: int = KH_PORT_TCP
    roster_rev: int = 0
    flags: int = 0
    name_rev: int = 0
    name: str = ""

    def encode(self) -> bytes:
        return struct.pack("<IHHIBI", self.hub_id, self.http_port, self.tcp_port, self.roster_rev & 0xFFFFFFFF,
                           self.flags, self.name_rev & 0xFFFFFFFF) + _w_name(self.name)

    @classmethod
    def decode(cls, data: bytes) -> "Offer":
        r = _Reader(data)
        m = cls(r.u32(), r.u16(), r.u16(), r.u32(), r.u8(), r.u32())
        m.name = r.name()
        if not m.http_port or not m.tcp_port:
            raise WireError("ports")
        return m


def token_valid(token: str) -> bool:
    return len(token) == KJ_REG_TOKEN_LEN and all(c in TOKEN_ALPHABET for c in token)


def encode_reg(token: str) -> bytes:
    if not token_valid(token):
        raise WireError("token")
    return token.encode("ascii")


def decode_reg(data: bytes) -> str:
    if len(data) != KJ_REG_TOKEN_LEN:
        raise WireError("token length")
    token = data.decode("ascii", "replace")
    if not token_valid(token):
        raise WireError("token")
    return token


@dataclass
class RegState:
    token: str
    state: int
    name_rev: int = 0
    name: str = ""

    def encode(self) -> bytes:
        return encode_reg(self.token) + struct.pack("<BI", self.state, self.name_rev & 0xFFFFFFFF) + _w_name(self.name)

    @classmethod
    def decode(cls, data: bytes) -> "RegState":
        r = _Reader(data)
        m = cls(decode_reg(r.take(KJ_REG_TOKEN_LEN)), r.u8(), r.u32())
        m.name = r.name()
        if m.state > KH_REG_DONE:
            raise WireError("state")
        return m


def encode_name_get(nos: list[int]) -> bytes:
    if not 1 <= len(nos) <= KH_NAMES_MAX or any(not 1 <= n <= 255 for n in nos):
        raise WireError("nos")
    return bytes([len(nos)]) + bytes(nos)


def decode_name_get(data: bytes) -> list[int]:
    if len(data) < 1 or not 1 <= data[0] <= KH_NAMES_MAX or len(data) != data[0] + 1:
        raise WireError("name_get")
    nos = list(data[1:])
    if 0 in nos:
        raise WireError("no 0")
    return nos


@dataclass
class NameEntry:
    no: int
    flags: int = 0
    name: str = ""


@dataclass
class Names:
    roster_rev: int = 0
    entries: list[NameEntry] = field(default_factory=list)

    def encode(self) -> bytes:
        if len(self.entries) > KH_NAMES_MAX:
            raise WireError("too many")
        out = struct.pack("<IB", self.roster_rev & 0xFFFFFFFF, len(self.entries))
        for e in self.entries:
            out += bytes([e.no, e.flags]) + _w_name(e.name)
        return out

    @classmethod
    def decode(cls, data: bytes) -> "Names":
        r = _Reader(data)
        m = cls(r.u32())
        count = r.u8()
        if count > KH_NAMES_MAX:
            raise WireError("count")
        for _ in range(count):
            e = NameEntry(r.u8(), r.u8())
            e.name = r.name()
            if e.no == 0:
                raise WireError("no 0")
            m.entries.append(e)
        if r.pos != len(data):
            raise WireError("trailing")
        return m


def frame_info(payload: bytes) -> tuple[int, int] | None:
    """游戏帧头：返回 (类型, 赌局号)；不是本版本的游戏帧返回 None。"""
    if len(payload) < KJ_FRAME_HEADER or payload[0:2] != b"KJ" or payload[2] != KJ_PROTO_VERSION:
        return None
    return payload[3], payload[4] | (payload[5] << 8)


def mac_text(mac: bytes) -> str:
    return mac.hex()
