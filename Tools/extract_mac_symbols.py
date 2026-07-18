"""Extract debug symbols from the Fable TLC Mac (Feral, 2008) binary.

The Mac port ships with full debug symbols — every function name with its
demangled C++ signature. Dumping them gives a near-complete function-name map
to port onto the PC exe (match by class + method, then confirm addresses), the
single biggest reverse-engineering accelerator for this game.

This runs on Windows (no macOS tools needed) via LIEF, which parses Mach-O and
demangles. Install once:  pip install lief

Usage:
    python extract_mac_symbols.py <path-to-mac-Fable-binary> [outdir]

The Mac binary is the actual game executable inside the app bundle, typically
  "Fable The Lost Chapters.app/Contents/MacOS/Fable The Lost Chapters"
(may be a fat/universal PPC+i386 Mach-O; we read the i386 slice, matching the
PC x86 build). Obtain it from a legitimately-owned copy — see
docs/community-resources.md.

Outputs (into outdir, default ./mac-symbols/):
  mac-symbols-all.txt      every function symbol, demangled, sorted
  mac-symbols.tsv          address<TAB>demangled<TAB>mangled
  mac-vs-pc-rtti.tsv       classes present in BOTH the Mac symbols and our
                           PC RTTI table (docs/data/rtti-classes.tsv) — the
                           high-confidence cross-reference set
"""
import sys, os, re

def die(msg):
    print("ERROR:", msg); sys.exit(1)

try:
    import lief
except ImportError:
    die("LIEF not installed. Run:  pip install lief")

if len(sys.argv) < 2:
    die("usage: python extract_mac_symbols.py <mac-binary> [outdir]")

path = sys.argv[1]
outdir = sys.argv[2] if len(sys.argv) > 2 else "mac-symbols"
os.makedirs(outdir, exist_ok=True)
if not os.path.exists(path):
    die("binary not found: " + path)

fat = lief.MachO.parse(path)
if fat is None:
    die("not a Mach-O binary (LIEF could not parse it)")

# Pick the i386 slice from a fat binary (matches the PC x86 build); fall back
# to the first/only slice.
bin_ = None
for b in fat:
    cpu = str(b.header.cpu_type)
    if "x86" in cpu.lower() and "64" not in cpu:   # i386
        bin_ = b; break
if bin_ is None:
    bin_ = fat.at(0) if hasattr(fat, "at") else list(fat)[0]
print("slice:", bin_.header.cpu_type)

rows = []
seen = set()
for sym in bin_.symbols:
    name = sym.name or ""
    if not name:
        continue
    # Function symbols in Mach-O C++ are mangled as _ZN... (Itanium ABI, with a
    # leading underscore on Mach-O). Keep those; skip data/section noise.
    stripped = name[1:] if name.startswith("_") else name
    if not stripped.startswith("_Z"):
        continue
    try:
        demangled = lief.demangle(stripped)  # LIEF >= 0.14
    except Exception:
        demangled = None
    demangled = demangled or getattr(sym, "demangled_name", "") or stripped
    addr = getattr(sym, "value", 0) or 0
    key = (addr, demangled)
    if key in seen:
        continue
    seen.add(key)
    rows.append((addr, demangled, stripped))

rows.sort(key=lambda r: r[1].lower())
print("function symbols:", len(rows))

with open(os.path.join(outdir, "mac-symbols-all.txt"), "w", encoding="utf-8") as f:
    f.write("# Fable TLC Mac (Feral) demangled function symbols (%d)\n" % len(rows))
    for _, dem, _m in rows:
        f.write(dem + "\n")

with open(os.path.join(outdir, "mac-symbols.tsv"), "w", encoding="utf-8") as f:
    f.write("addr\tdemangled\tmangled\n")
    for addr, dem, man in rows:
        f.write("%08X\t%s\t%s\n" % (addr, dem, man))

# Cross-reference class names against the PC RTTI table.
here = os.path.dirname(os.path.abspath(__file__))
rtti = os.path.join(here, "..", "docs", "data", "rtti-classes.tsv")
pc_classes = set()
if os.path.exists(rtti):
    for line in open(rtti, encoding="utf-8"):
        if line.startswith(("#", "VFT")):
            continue
        cols = line.split("\t")
        if len(cols) >= 3:
            pc_classes.add(cols[2])

def class_of(dem):
    # "CFoo::Bar(args)" -> "CFoo"; handle CFoo::CBar::method too (take up to
    # the last :: before the method name).
    m = re.match(r"([\w:<>]+)::[~\w]+\s*\(", dem)
    if not m:
        return None
    return m.group(1).split("<")[0]

matched = {}
for addr, dem, man in rows:
    c = class_of(dem)
    if c and c in pc_classes:
        matched.setdefault(c, []).append((addr, dem))

with open(os.path.join(outdir, "mac-vs-pc-rtti.tsv"), "w", encoding="utf-8") as f:
    f.write("# Classes in BOTH the Mac symbols and docs/data/rtti-classes.tsv\n")
    f.write("# class\tmac_methods\n")
    for c in sorted(matched):
        f.write("%s\t%d\n" % (c, len(matched[c])))
print("classes matching PC RTTI:", len(matched), "of", len(pc_classes), "PC classes")
print("wrote:", outdir)
