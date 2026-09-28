import base64
import json
import re
from datetime import datetime, timezone
from pathlib import Path


def _utc_now():
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def _slug(text):
    text = str(text).strip().lower()
    text = re.sub(r"[^a-z0-9._-]+", "-", text)
    text = text.strip("-")
    return text or "artifact"


def _addr_key(addr):
    if isinstance(addr, str):
        text = addr.strip()
        if text.startswith("$"):
            value = int(text[1:], 16)
        elif text.lower().startswith("0x"):
            value = int(text, 16)
        else:
            value = int(text, 10)
    else:
        value = int(addr)
    return f"${value & 0xFFFF:04x}"


def _json_default(value):
    if isinstance(value, bytes):
        return {"base64": base64.b64encode(value).decode("ascii")}
    if isinstance(value, Path):
        return str(value)
    raise TypeError(f"not JSON serializable: {type(value).__name__}")


class Project:
    def __init__(self, root):
        self.root = Path(root)
        self.path = self.root / "project.json"
        self.data = None

    @classmethod
    def open(cls, root):
        project = cls(root)
        project.root.mkdir(parents=True, exist_ok=True)
        project._ensure_dirs()
        if project.path.exists():
            with project.path.open("r", encoding="utf-8") as f:
                project.data = json.load(f)
        else:
            project.data = {
                "schema": "fceux-bridge-project-v1",
                "created_at": _utc_now(),
                "updated_at": _utc_now(),
                "source": {},
                "labels": {},
                "comments": {},
                "regions": [],
                "notes": [],
                "findings": [],
                "artifacts": [],
            }
            project.save()
        return project

    def _ensure_dirs(self):
        for name in ("screenshots", "memory", "history", "traces", "rom", "reports"):
            (self.root / name).mkdir(parents=True, exist_ok=True)

    def save(self):
        self.data["updated_at"] = _utc_now()
        self.root.mkdir(parents=True, exist_ok=True)
        self._ensure_dirs()
        with self.path.open("w", encoding="utf-8") as f:
            json.dump(self.data, f, indent=2, sort_keys=True, default=_json_default)
            f.write("\n")

    def set_source(self, *, rom=None, cart_info=None):
        if rom is not None:
            self.data["source"]["rom"] = str(rom)
        if cart_info is not None:
            self.data["source"]["cart_info"] = cart_info
            if cart_info.get("md5"):
                self.data["source"]["md5"] = cart_info["md5"]
            if cart_info.get("mapper") is not None:
                self.data["source"]["mapper"] = cart_info["mapper"]
        self.save()

    def add_label(self, addr, name, *, bank=None, source="agent"):
        key = _addr_key(addr)
        self.data["labels"][key] = {
            "name": str(name),
            "bank": bank,
            "source": source,
            "updated_at": _utc_now(),
        }
        self.save()
        return key

    def add_comment(self, addr, text, *, bank=None, source="agent"):
        key = _addr_key(addr)
        self.data["comments"].setdefault(key, [])
        entry = {"text": str(text), "bank": bank, "source": source, "created_at": _utc_now()}
        self.data["comments"][key].append(entry)
        self.save()
        return entry

    def add_region(self, start, end, name, *, kind="unknown", bank=None, source="agent"):
        entry = {
            "start": _addr_key(start),
            "end": _addr_key(end),
            "name": str(name),
            "kind": str(kind),
            "bank": bank,
            "source": source,
            "created_at": _utc_now(),
        }
        self.data["regions"].append(entry)
        self.save()
        return entry

    def add_note(self, text, *, title=None, tags=None, source="agent"):
        entry = {
            "title": title,
            "text": str(text),
            "tags": list(tags or []),
            "source": source,
            "created_at": _utc_now(),
        }
        self.data["notes"].append(entry)
        self.save()
        return entry

    def add_finding(self, title, *, detail=None, severity="info", addresses=None, artifacts=None, tags=None):
        entry = {
            "title": str(title),
            "detail": detail,
            "severity": severity,
            "addresses": [_addr_key(addr) for addr in (addresses or [])],
            "artifacts": list(artifacts or []),
            "tags": list(tags or []),
            "created_at": _utc_now(),
        }
        self.data["findings"].append(entry)
        self.save()
        return entry

    def add_artifact(self, kind, path, *, metadata=None):
        rel = Path(path)
        if rel.is_absolute():
            rel = rel.relative_to(self.root)
        entry = {
            "kind": str(kind),
            "path": rel.as_posix(),
            "metadata": metadata or {},
            "created_at": _utc_now(),
        }
        self.data["artifacts"].append(entry)
        self.save()
        return entry

    def write_json_artifact(self, kind, name, payload, *, directory="reports", metadata=None):
        path = self.root / directory / f"{_slug(name)}.json"
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("w", encoding="utf-8") as f:
            json.dump(payload, f, indent=2, sort_keys=True, default=_json_default)
            f.write("\n")
        return self.add_artifact(kind, path, metadata=metadata)

    def write_binary_artifact(self, kind, name, data, *, directory, suffix, metadata=None):
        path = self.root / directory / f"{_slug(name)}{suffix}"
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("wb") as f:
            f.write(bytes(data))
        return self.add_artifact(kind, path, metadata=metadata)

    def capture_rawscreen(self, bridge, name="screen", *, overlay=False):
        screen = bridge.rawscreen(overlay=overlay)
        meta = {k: v for k, v in screen.items() if k not in ("pixels", "base64")}
        return self.write_binary_artifact("rawscreen", name, screen["pixels"], directory="screenshots", suffix=".rgba", metadata=meta)

    def capture_memory(self, bridge, addr, length, name=None):
        dump = bridge.memdump(addr, length)
        meta = {k: v for k, v in dump.items() if k not in ("bytes", "base64")}
        return self.write_binary_artifact("memory", name or f"mem-{_addr_key(addr)}-{length}", dump["bytes"], directory="memory", suffix=".bin", metadata=meta)

    def capture_history(self, bridge, name="history", *, count=256, include_disasm=True):
        history = bridge.history(count=count, include_disasm=include_disasm)
        return self.write_json_artifact("history", name, history, directory="history")

    def capture_cdlog(self, bridge, name="cdlog", *, domain="all"):
        dump = bridge.cdlog_dump(domain=domain)
        summary = {"domain": domain, "status": dump.get("status")}
        artifacts = [self.write_json_artifact("cdlog-summary", f"{name}-summary", summary, directory="traces")]
        for key in ("cpu", "ppu"):
            item = dump.get(key)
            if item and item.get("bytes") is not None:
                artifacts.append(
                    self.write_binary_artifact(
                        f"cdlog-{key}",
                        f"{name}-{key}",
                        item["bytes"],
                        directory="traces",
                        suffix=".cdl",
                        metadata={"size": item.get("size")},
                    )
                )
        return artifacts

    def capture_rom_metadata(self, bridge, name="rom-metadata"):
        cart = bridge.cart_info()
        memmap = bridge.memmap()
        self.set_source(cart_info=cart)
        return self.write_json_artifact("rom-metadata", name, {"cart_info": cart, "memmap": memmap}, directory="rom")
