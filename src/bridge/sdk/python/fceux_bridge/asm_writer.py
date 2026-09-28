from pathlib import Path


def export_disassembly(bridge, path, start, count=256, *, labels=None, comments=None):
    labels = labels or {}
    comments = comments or {}
    rows = bridge.disasm(start, count)["instructions"]
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as f:
        for row in rows:
            addr = row["addr"].lower()
            if addr in labels:
                f.write(f"{labels[addr]}:\n")
            text = row["text"]
            comment = comments.get(addr)
            if comment:
                text = f"{text} ; {comment}"
            f.write(f"  {text}\n")
    return path


def project_labels(project):
    return {addr.lower(): item["name"] for addr, item in project.data.get("labels", {}).items()}


def project_comments(project):
    out = {}
    for addr, entries in project.data.get("comments", {}).items():
        if entries:
            out[addr.lower()] = entries[-1]["text"]
    return out
