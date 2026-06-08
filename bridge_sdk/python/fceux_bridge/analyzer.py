def diff_bytes(before, after, *, base=0):
    before = bytes(before)
    after = bytes(after)
    changes = []
    for index, (old, new) in enumerate(zip(before, after)):
        if old != new:
            changes.append({"addr": f"${(int(base) + index) & 0xFFFF:04x}", "offset": index, "before": old, "after": new})
    extra_start = min(len(before), len(after))
    for index in range(extra_start, len(after)):
        changes.append({"addr": f"${(int(base) + index) & 0xFFFF:04x}", "offset": index, "before": None, "after": after[index]})
    for index in range(extra_start, len(before)):
        changes.append({"addr": f"${(int(base) + index) & 0xFFFF:04x}", "offset": index, "before": before[index], "after": None})
    return changes


def memory_diff(bridge, addr, length, action):
    before = bridge.memdump(addr, length)["bytes"]
    action()
    after = bridge.memdump(addr, length)["bytes"]
    return diff_bytes(before, after, base=int(addr))


def changed_addresses(diff):
    return [entry["addr"] for entry in diff]


def first_matches(search_response):
    return list(search_response.get("matches", []))
