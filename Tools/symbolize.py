"""Symbolize an EgoMP crash log.

CrashDiag writes stack frames as "Module+0xOFFSET" (e.g. Fable.exe+0x0059A861).
This resolves every Fable.exe frame to the nearest known symbol from
docs/data/symbols.tsv (built by build_symbols.py), turning a raw crash stack
into named functions so you can read it at a glance.

Usage:
    python symbolize.py <EgoMP-crash-<pid>.log>       # symbolize a whole log
    python symbolize.py 0x0059A861                     # resolve one offset
    python symbolize.py --va 0x0059A861               # resolve one VA

Frames in EgoMP.dll can't be named (no symbols), non-Fable modules are left
as-is. A resolved frame reads:  Fable.exe+0x0059A861  ->  CWorld::Foo +0x21
"""
import sys, os, re

BASE = 0x400000
# Deltas beyond this from the nearest known symbol are treated as "unmapped
# region" -- our symbol set is sparse, so a big gap means we just don't know
# the function, not that it's a huge one. ~3 KB comfortably covers real funcs.
NEAR = 0xC00
HERE = os.path.dirname(os.path.abspath(__file__))
SYMS = os.path.join(HERE, "..", "docs", "data", "symbols.tsv")

def load_symbols():
    syms = []  # (addr, name, kind)
    if not os.path.exists(SYMS):
        sys.exit("symbol db not found: %s (run build_symbols.py)" % SYMS)
    for line in open(SYMS, encoding="utf-8"):
        if line.startswith(("#", "addr")):
            continue
        c = line.rstrip("\n").split("\t")
        if len(c) >= 3:
            syms.append((int(c[0], 16), c[1], c[2]))
    syms.sort()
    return syms

def resolve(va, syms):
    """Nearest symbol at or below va; returns (name, delta, kind) or None."""
    lo, hi, best = 0, len(syms) - 1, None
    while lo <= hi:
        mid = (lo + hi) // 2
        if syms[mid][0] <= va:
            best = mid; lo = mid + 1
        else:
            hi = mid - 1
    if best is None:
        return None
    addr, name, kind = syms[best]
    return name, va - addr, kind

# Frames the game/CRT throw from all the time -- annotate so they're recognizable.
KNOWN_MODULE_HINTS = {
    "KERNELBASE.dll": "(RaiseException / Win32 API -- the throw/relay site)",
    "MSVCR71.dll": "(MSVC 7.1 CRT -- _CxxThrowException / std lib)",
    "MSVCP71.dll": "(MSVC 7.1 C++ stdlib)",
}

def symbolize_line(line, syms):
    m = re.search(r"(Fable\.exe)\+0x([0-9A-Fa-f]+)", line)
    if m:
        off = int(m.group(2), 16)
        va = BASE + off
        r = resolve(va, syms)
        if r:
            name, delta, kind = r
            # A large delta means the target isn't itself a known symbol; the
            # name is just the nearest label below and probably a DIFFERENT,
            # unmapped function. Only small deltas are trustworthy.
            if delta > NEAR:
                suffix = "  ->  ~0x%X past %s (unmapped region -- name NOT reliable)" % (delta, name)
            else:
                tag = " [%s]" % kind if kind != "func" else ""
                suffix = "  ->  %s +0x%X%s" % (name, delta, tag)
            return line.rstrip("\n") + suffix
        return line.rstrip("\n") + "  ->  (no symbol <= 0x%08X)" % va
    m2 = re.search(r"([A-Za-z0-9_]+\.dll)\+0x[0-9A-Fa-f]+", line)
    if m2 and m2.group(1) in KNOWN_MODULE_HINTS:
        return line.rstrip("\n") + "  " + KNOWN_MODULE_HINTS[m2.group(1)]
    return line.rstrip("\n")

def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    syms = load_symbols()
    arg = sys.argv[1]

    # Single-address mode.
    if arg == "--va" and len(sys.argv) > 2:
        va = int(sys.argv[2], 16)
    elif re.fullmatch(r"0x[0-9A-Fa-f]+", arg):
        off = int(arg, 16)
        va = off if off >= BASE else BASE + off
    else:
        va = None

    if va is not None:
        r = resolve(va, syms)
        if r:
            name, delta, kind = r
            print("0x%08X  (Fable.exe+0x%X)  ->  %s +0x%X [%s]"
                  % (va, va - BASE, name, delta, kind))
        else:
            print("0x%08X: no symbol" % va)
        return

    # Log-file mode.
    path = arg
    if not os.path.exists(path):
        sys.exit("not found: " + path)
    print("# symbolized %s  (%d symbols loaded)\n" % (os.path.basename(path), len(syms)))
    for line in open(path, encoding="utf-8", errors="replace"):
        print(symbolize_line(line, syms))

if __name__ == "__main__":
    main()
