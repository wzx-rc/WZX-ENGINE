# Colours that exist for real, per part: Rebrickable's free downloads (elements.csv = the elements LEGO produced,
# inventory_parts.csv = the parts of every set inventory), part numbers in the LDraw naming (Rebrickable's part_num is
# the LDraw file name for nearly every part), colour ids = LDraw colour codes (Rebrickable numbers its colours after
# LDraw). Mould variants and alternates (part_relationships M / A) share their colours.
# Data: Rebrickable (https://rebrickable.com/downloads/), used under their terms with credit.
# Usage: py tools/brickit/part_colors.py build/raw/rebrickable assets/brickit/part_colors.txt
import csv, gzip, io, sys
from collections import defaultdict

src, out = sys.argv[1], sys.argv[2]

def rows(name):
    with gzip.open(f"{src}/{name}.csv.gz", "rt", encoding="utf-8", newline="") as f:
        yield from csv.DictReader(f)

colours = defaultdict(set)
for r in rows("elements"):
    colours[r["part_num"].lower()].add(int(r["color_id"]))
for r in rows("inventory_parts"):
    if r["is_spare"] == "True":
        continue
    colours[r["part_num"].lower()].add(int(r["color_id"]))

# mould variants and alternates: the same brick, the same colours (a union over each connected set)
parent = {}
def find(x):
    while parent.get(x, x) != x:
        parent[x] = parent.get(parent[x], parent[x])
        x = parent[x]
    return x
for r in rows("part_relationships"):
    if r["rel_type"] in ("M", "A"):
        a, b = find(r["child_part_num"].lower()), find(r["parent_part_num"].lower())
        if a != b:
            parent[a] = b
groups = defaultdict(set)
for p in list(colours.keys()) + list(parent.keys()):
    groups[find(p)].add(p)
for members in groups.values():
    u = set()
    for p in members:
        u |= colours.get(p, set())
    for p in members:
        if u:
            colours[p] = set(u)

names = {int(r["id"]): r["name"] for r in rows("colors")}
lines = []
for p in sorted(colours):
    c = sorted(x for x in colours[p] if x >= 0)
    if c:
        lines.append(p + " " + " ".join(map(str, c)))
with open(out, "w", encoding="ascii", newline="\n") as f:
    f.write("# BrickIt: LDraw colour codes that exist for each part (LDraw name). Source: Rebrickable downloads "
            "(elements, inventory_parts, part_relationships M / A), https://rebrickable.com\n")
    f.write("\n".join(lines) + "\n")
print(len(lines), "parts,", sum(len(colours[p]) for p in colours), "part / colour pairs,", len(names), "colours")
