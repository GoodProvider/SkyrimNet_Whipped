"""Repoint the whip NIFs from textures\\weapons\\DOM\\ to textures\\weapons\\SNW\\.

NIF strings are length-prefixed, so the replacement must keep the same length
("DOM" -> "SNW"). Run once after copying the meshes from PAH Diary Of Mine.
"""
import pathlib
import sys

OLD = rb"textures\weapons\DOM" + b"\\"
NEW = rb"textures\weapons\SNW" + b"\\"
assert len(OLD) == len(NEW)

root = pathlib.Path(__file__).resolve().parent.parent / "meshes" / "weapons" / "SNW"
for nif in sorted(root.glob("*.nif")):
    data = nif.read_bytes()
    count = data.lower().count(OLD.lower())
    if count == 0:
        print(f"{nif.name}: already patched")
        continue
    patched = bytearray(data)
    lower = data.lower()
    start = 0
    while (i := lower.find(OLD.lower(), start)) != -1:
        patched[i:i + len(NEW)] = NEW
        start = i + len(NEW)
    nif.write_bytes(bytes(patched))
    print(f"{nif.name}: patched {count} path(s)")

sys.exit(0)
