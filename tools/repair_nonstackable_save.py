"""Remove explicitly selected duplicate item records from an offline Crashlands save.

The six list layouts are verified against gml_Script_load_binary_globals and
gml_Script_buffer_read_to_list in the shipped Steam build 20483884.
Defaults to a dry run. Requires an exact, previously verified backup to apply.
"""
import argparse
import collections
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import struct


LAYOUT = (
    ("items_owned", 104, "H"),
    ("itemstacks", 105, "H"),
    ("itemdata", 106, "f"),
    ("recent_pickups", 101, "h"),
    ("inventory", 107, "h"),
    ("equipinv", 108, "h"),
)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def game_running():
    # Process names only; no game memory is read or modified.
    class ProcessEntry(ctypes.Structure):
        _fields_ = [("size", wintypes.DWORD), ("usage", wintypes.DWORD),
                    ("pid", wintypes.DWORD), ("heap", ctypes.c_size_t),
                    ("module", wintypes.DWORD), ("threads", wintypes.DWORD),
                    ("parent", wintypes.DWORD), ("priority", wintypes.LONG),
                    ("flags", wintypes.DWORD), ("exe", wintypes.WCHAR * 260)]
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
    kernel.Process32FirstW.argtypes = [wintypes.HANDLE, ctypes.POINTER(ProcessEntry)]
    kernel.Process32NextW.argtypes = [wintypes.HANDLE, ctypes.POINTER(ProcessEntry)]
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    handle = kernel.CreateToolhelp32Snapshot(2, 0)
    require(handle not in (None, ctypes.c_void_p(-1).value), "Cannot enumerate processes")
    try:
        entry = ProcessEntry()
        entry.size = ctypes.sizeof(entry)
        require(kernel.Process32FirstW(handle, ctypes.byref(entry)), "Cannot enumerate processes")
        while True:
            if entry.exe.lower() == "crashlands.exe":
                return True
            if not kernel.Process32NextW(handle, ctypes.byref(entry)):
                return False
    finally:
        kernel.CloseHandle(handle)


def parse(data):
    require(data[:4] == b"\xde\x00\xc9\x00", "Unsupported save header")
    marker = b"\xca\x00\x68\x00"
    require(data.count(marker) == 1, "Inventory section is ambiguous")
    start = data.index(marker) + 2
    pos = start
    values = {}
    for name, tag, fmt in LAYOUT:
        actual, count = struct.unpack_from("<HI", data, pos)
        require(actual == tag and count <= 32767, f"Unexpected {name} list")
        pos += 6
        size = struct.calcsize(fmt) * count
        require(pos + size <= len(data), f"Truncated {name} list")
        values[name] = list(struct.unpack_from(f"<{count}{fmt}", data, pos))
        pos += size
    require(data[pos:pos + 2] == b"\xcb\x00", "Equipment section marker is missing")
    require(len(values["items_owned"]) == len(values["itemstacks"]) == len(values["itemdata"]),
            "Item database lists have different lengths")
    return start, pos, values


def serialize(values):
    return b"".join(struct.pack("<HI", tag, len(values[name])) +
                    struct.pack(f"<{len(values[name])}{fmt}", *values[name])
                    for name, tag, fmt in LAYOUT)


def repair(data, items):
    start, end, old = parse(data)
    require(serialize(old) == data[start:end], "Save does not round-trip exactly")
    count = len(old["items_owned"])
    inventory = old["inventory"]
    equipinv = old["equipinv"]
    require(all(0 <= ref < count for ref in inventory), "Invalid inventory reference")
    require(all(-1 <= ref < count for ref in equipinv), "Invalid equipment reference")
    refs = inventory + [ref for ref in equipinv if ref >= 0]
    require(len(refs) == len(set(refs)) == count, "Unexpected shared or unreferenced database entries")
    removed = set()
    details = []
    for item in items:
        indices = [i for i, value in enumerate(old["items_owned"]) if value == item]
        require(len(indices) >= 1000, f"Item {item} has no mass-duplication signature")
        require(all(old["itemstacks"][i] == 1 and old["itemdata"][i] == 0 for i in indices),
                f"Item {item} has distinct instance data; refusing to discard it")
        equipped = [i for i in indices if i in equipinv]
        require(len(equipped) <= 1, f"Item {item} is equipped in multiple slots")
        kept = equipped[0] if equipped else indices[0]
        removed.update(set(indices) - {kept})
        details.append(dict(item_id=item, before=len(indices), after=1,
                            removed=len(indices)-1, retained_old_index=kept,
                            equipped=bool(equipped)))
    retained = [i for i in range(count) if i not in removed]
    remap = {old_index: new_index for new_index, old_index in enumerate(retained)}
    new = {name: [old[name][i] for i in retained]
           for name in ("items_owned", "itemstacks", "itemdata")}
    new["recent_pickups"] = old["recent_pickups"][:]  # Item IDs, not database indices.
    new["inventory"] = [remap[i] for i in inventory if i not in removed]
    new["equipinv"] = [remap[i] if i >= 0 else -1 for i in equipinv]
    output = data[:start] + serialize(new) + data[end:]
    new_start, new_end, actual = parse(output)
    require(actual == new, "Repaired save failed to parse")
    require(output[:new_start] == data[:start] and output[new_end:] == data[end:],
            "Unrelated save sections changed")
    for name in ("items_owned", "itemstacks", "itemdata"):
        require(actual[name] == [old[name][i] for i in retained], f"Retained {name} changed")
    require(collections.Counter(old["items_owned"][i] for i in removed) ==
            collections.Counter({d["item_id"]: d["removed"] for d in details}),
            "An unrelated item was removed")
    for old_ref, new_ref in zip(equipinv, actual["equipinv"]):
        require(old_ref == new_ref == -1 or
                all(old[name][old_ref] == actual[name][new_ref]
                    for name in ("items_owned", "itemstacks", "itemdata")),
                "An equipped item changed")
    report = dict(items=details, removed_records=len(removed),
                  database_before=count, database_after=len(retained),
                  inventory_before=len(inventory), inventory_after=len(new["inventory"]),
                  bytes_before=len(data), bytes_after=len(output),
                  sha256_before=sha(data), sha256_after=sha(output),
                  unrelated_sections_byte_identical=True, equipped_items_preserved=True)
    return output, report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--save", required=True, type=Path)
    parser.add_argument("--backup", required=True, type=Path)
    parser.add_argument("--item", required=True, type=int, action="append")
    parser.add_argument("--report", required=True, type=Path)
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    require(not game_running(), "Exit Crashlands before repairing its save")
    data = args.save.read_bytes()
    require(data == args.backup.read_bytes(), "Save differs from the verified backup")
    output, report = repair(data, sorted(set(args.item)))
    report.update(save_path=str(args.save.resolve()), backup_path=str(args.backup.resolve()),
                  applied=False, in_game_load_test=False)
    candidate = args.report.with_suffix(".global_bin")
    candidate.write_bytes(output)
    require(candidate.read_bytes() == output, "Candidate write verification failed")
    if args.apply:
        require(not game_running(), "Crashlands started during repair")
        require(args.save.read_bytes() == data, "Save changed during repair")
        temporary = args.save.with_name("global_bin.repair.tmp")
        require(not temporary.exists(), "Repair temporary file already exists")
        temporary.write_bytes(output)
        require(temporary.read_bytes() == output, "Repair write verification failed")
        os.replace(temporary, args.save)
        require(args.save.read_bytes() == output, "Saved repair failed verification")
        report["applied"] = True
    args.report.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps(report, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
