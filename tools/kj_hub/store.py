"""hub 的数据目录：配置（hub_id、管理令牌）、昵称登记表、每局一个 JSONL 记录、CSV 导出。只用标准库。

数据目录默认在 ~/kj-hub-data（可用 --data 改），里面有昵称等个人数据，不要提交到仓库。
"""

from __future__ import annotations

import csv
import io
import json
import os
import re
import secrets
import time
from pathlib import Path


def atomic_write(path: Path, text: str) -> None:
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_text(text, encoding="utf-8")
    os.replace(tmp, path)


def iso(ts: float) -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%S", time.localtime(ts))


class Registry:
    """设备 MAC（12 位十六进制）→ 昵称。删除用"已删除"标记，保证 hub 说了算（设备那边会跟着清掉）。"""

    def __init__(self, path: Path, log_path: Path, wall=time.time):
        self.path = path
        self.log_path = log_path
        self.wall = wall
        self.devices: dict[str, dict] = {}
        if path.exists():
            try:
                data = json.loads(path.read_text(encoding="utf-8"))
                self.devices = {k: v for k, v in data.get("devices", {}).items() if re.fullmatch(r"[0-9a-f]{12}", k)}
            except (OSError, ValueError):
                self.devices = {}

    def get(self, mac: str) -> dict | None:
        return self.devices.get(mac)

    def name_of(self, mac: str) -> str:
        e = self.devices.get(mac)
        return e["name"] if e and not e.get("deleted") else ""

    def owner_of(self, name: str) -> str | None:
        key = name.casefold()
        for mac, e in self.devices.items():
            if not e.get("deleted") and e.get("name", "").casefold() == key:
                return mac
        return None

    def _put(self, mac: str, name: str, rev: int, source: str, deleted: bool = False) -> dict:
        entry = {"name": name, "rev": rev, "deleted": deleted, "source": source, "updated": iso(self.wall())}
        self.devices[mac] = entry
        self.save()
        with self.log_path.open("a", encoding="utf-8") as f:
            f.write(json.dumps({"ts": iso(self.wall()), "mac": mac, **entry}, ensure_ascii=False) + "\n")
        return entry

    def set(self, mac: str, name: str, source: str) -> dict:
        old = self.devices.get(mac)
        return self._put(mac, name, (old["rev"] if old else 0) + 1, source)

    def adopt(self, mac: str, name: str, rev: int) -> dict:
        """hub 没有记录、设备却带着昵称来（例如换了电脑 / 删了数据目录）：按设备的为准补回。"""
        return self._put(mac, name, max(rev, 1), "device")

    def delete(self, mac: str, source: str) -> dict | None:
        old = self.devices.get(mac)
        if not old or old.get("deleted"):
            return None
        return self._put(mac, "", old["rev"] + 1, source, deleted=True)

    def clear_all(self, source: str) -> int:
        n = 0
        for mac in list(self.devices):
            if self.delete(mac, source):
                n += 1
        return n

    def save(self) -> None:
        atomic_write(self.path, json.dumps({"version": 1, "devices": self.devices}, ensure_ascii=False, indent=1))

    def csv(self) -> str:
        buf = io.StringIO()
        w = csv.writer(buf)
        w.writerow(["设备", "昵称", "更新时间", "来源"])
        for mac, e in sorted(self.devices.items()):
            if e.get("deleted"):
                continue
            w.writerow([mac[-4:].upper(), e["name"], e.get("updated", ""), e.get("source", "")])
        return buf.getvalue()


class SessionLog:
    """每个赌局号 + 局号一个 JSONL 文件：sessions/20261004-201530_A3F2_g1.jsonl。"""

    def __init__(self, root: Path, wall=time.time):
        self.root = root
        self.wall = wall
        self.root.mkdir(parents=True, exist_ok=True)
        self.files: dict[tuple[str, int], Path] = {}

    def path_for(self, room: str, gid: int) -> Path:
        key = (room, gid)
        if key not in self.files:
            stamp = time.strftime("%Y%m%d-%H%M%S", time.localtime(self.wall()))
            self.files[key] = self.root / f"{stamp}_{room}_g{gid}.jsonl"
        return self.files[key]

    def write(self, room: str, gid: int, obj: dict) -> None:
        rec = {"ts": iso(self.wall()), "room": room, "gid": gid, **obj}
        with self.path_for(room, gid).open("a", encoding="utf-8") as f:
            f.write(json.dumps(rec, ensure_ascii=False) + "\n")

    def list(self) -> list[dict]:
        out = []
        for p in sorted(self.root.glob("*.jsonl"), reverse=True):
            m = re.fullmatch(r"(\d{8}-\d{6})_([0-9A-F]{4})_g(\d+)\.jsonl", p.name)
            if m:
                out.append({"file": p.name, "started": m.group(1), "room": m.group(2), "gid": int(m.group(3)),
                            "bytes": p.stat().st_size})
        return out

    def read(self, name: str) -> list[dict]:
        if not re.fullmatch(r"\d{8}-\d{6}_[0-9A-F]{4}_g\d+\.jsonl", name):
            raise FileNotFoundError(name)
        rows = []
        for line in (self.root / name).read_text(encoding="utf-8").splitlines():
            try:
                rows.append(json.loads(line))
            except ValueError:
                continue
        return rows


STATUS_CN = {"waiting": "等待开局", "idle": "空闲", "challenging": "发起挑战", "challenged": "被挑战", "duel": "对决中",
             "cleared": "过关", "out": "出局", "failed": "失败", "bumping": "碰拳中", "matched": "碰拳配对"}
EVENT_CN = {"join": "入座", "rejoin": "重连", "bot": "电脑入座", "remove": "离开", "start": "开局", "end": "结束",
            "new": "新一局", "reset": "清空", "challenge": "挑战", "cancel": "撤回挑战", "decline": "拒绝",
            "timeout": "挑战超时", "accept": "应战", "lock": "出牌", "result": "结算", "withdraw": "放弃",
            "abort": "断线作废", "cleared": "过关", "eliminated": "出局", "failed": "失败", "match": "碰拳配对",
            "match_cancel": "取消配对", "bump_fail": "碰拳失败"}
CARD_CN = {"r": "石头", "s": "剪刀", "p": "布", "": ""}
FINAL_CN = {"nocards": "手牌出完星星不足", "timeup": "时间到仍有手牌", "": ""}


def _csv_text(rows: list[list]) -> str:
    buf = io.StringIO()
    csv.writer(buf).writerows(rows)
    return buf.getvalue()


def players_csv(records: list[dict]) -> str:
    """一局结束后每位选手的最终状态（取每个座位最后一行 p）。"""
    seats: dict[int, dict] = {}
    for r in records:
        if r.get("t") != "p":
            continue
        if r.get("gone"):
            seats.pop(r.get("no"), None)
        else:
            seats[r["no"]] = r
    rows = [["赌局号", "局号", "编号", "昵称", "设备", "电脑选手", "状态", "星星", "石头", "剪刀", "布", "胜", "负", "平", "结局原因"]]
    for no in sorted(seats):
        p = seats[no]
        rows.append([p.get("room", ""), p.get("gid", ""), no, p.get("name", ""), str(p.get("id", "")).upper()[-4:],
                     "是" if p.get("bot") else "", STATUS_CN.get(p.get("st", ""), p.get("st", "")), p.get("stars", 0),
                     p.get("r", 0), p.get("s", 0), p.get("p", 0), p.get("w", 0), p.get("l", 0), p.get("d", 0),
                     FINAL_CN.get(p.get("fin", ""), "")])
    return _csv_text(rows)


def events_csv(records: list[dict]) -> str:
    rows = [["时间", "赌局号", "局号", "事件", "A编号", "A昵称", "B编号", "B昵称", "A出牌", "B出牌", "胜者", "对决号", "附加"]]
    for r in records:
        if r.get("t") != "e" or r.get("k") == "rejoin":
            continue
        rows.append([r.get("ts", ""), r.get("room", ""), r.get("gid", ""), EVENT_CN.get(r.get("k", ""), r.get("k", "")),
                     r.get("a") or "", r.get("a_name", ""), r.get("b") or "", r.get("b_name", ""),
                     CARD_CN.get(r.get("ca", ""), ""), CARD_CN.get(r.get("cb", ""), ""), r.get("w") or "",
                     r.get("duel") or "", r.get("x") or ""])
    return _csv_text(rows)


class Store:
    def __init__(self, root: Path, wall=time.time):
        self.root = root.expanduser()
        self.root.mkdir(parents=True, exist_ok=True)
        cfg_path = self.root / "config.json"
        try:
            self.config = json.loads(cfg_path.read_text(encoding="utf-8")) if cfg_path.exists() else {}
        except ValueError:
            self.config = {}
        changed = False
        if not isinstance(self.config.get("hub_id"), int) or not self.config["hub_id"]:
            self.config["hub_id"] = secrets.randbits(31) | 1
            changed = True
        if not self.config.get("admin_token"):
            self.config["admin_token"] = secrets.token_urlsafe(12)
            changed = True
        if changed:
            atomic_write(cfg_path, json.dumps(self.config, indent=1))
        self.registry = Registry(self.root / "registry.json", self.root / "registry.log.jsonl", wall)
        self.sessions = SessionLog(self.root / "sessions", wall)

    @property
    def hub_id(self) -> int:
        return self.config["hub_id"]

    @property
    def admin_token(self) -> str:
        return self.config["admin_token"]
