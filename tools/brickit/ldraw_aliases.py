# The LDraw names that point to another part: aliases ("Part Alias", "=" titles) and the parts moved to a new number
# ("~Moved to ..."). Old and official models (OMR) still use them: Library::find resolves them through this table.
# Usage: py tools/brickit/ldraw_aliases.py <ldraw dir> <out: aliases.txt>
import os, re, sys

ldraw, out = sys.argv[1], sys.argv[2]
parts = os.path.join(ldraw, "parts")
rows = []
for f in sorted(os.listdir(parts)):
    if not f.lower().endswith(".dat"):
        continue
    with open(os.path.join(parts, f), encoding="latin1") as fh:
        lines = fh.read().splitlines()
    if not lines:
        continue
    title = lines[0][1:].strip() if lines[0].startswith("0") else ""
    kind = ""
    refs = []
    for l in lines:
        m = re.match(r"^0\s+!LDRAW_ORG\s+(\S+(?:\s+Alias)?)", l)
        if m:
            kind = m.group(1)
        if l.startswith("1 "):
            refs.append(l.split()[-1].lower())
    alias = "Alias" in kind or title.startswith("=") or title.startswith("~Moved to")
    if alias and len(refs) == 1:
        target = refs[0]
        if target.startswith("s\\") or target.startswith("s/"):
            continue
        rows.append(f[:-4].lower() + " " + target[:-4] if target.endswith(".dat") else f[:-4].lower() + " " + target)
with open(out, "w", encoding="ascii", newline="\n") as fh:
    fh.write("# LDraw alias / moved part -> target part (tools/brickit/ldraw_aliases.py)\n" + "\n".join(rows) + "\n")
print(len(rows), "aliases")
