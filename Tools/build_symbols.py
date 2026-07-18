"""Build a consolidated Fable.exe symbol database (docs/data/symbols.tsv).

Merges three sources of name<->address knowledge for the Steam TLC exe:
  1. FableMenu source: every Call/STDCall/FASTCall/CallMethod<...,0xADDR,...>
     paired with the enclosing C++ function name (authoritative name->address).
  2. docs/data/rtti-classes.tsv: class vtable addresses (kind=vtable).
  3. A small hand-curated set of engine statics/singletons (kind=data).

Output columns: addr<TAB>name<TAB>kind<TAB>conv<TAB>source
kind: func | vtable | data ; conv: thiscall/cdecl/stdcall/fastcall/-

Usage:
    python build_symbols.py <FableMenu-source-root> <egomp-repo-root>

The committed symbols.tsv is the artifact; re-run this only to regenerate it
(needs a checkout of github.com/ermaccer/FableMenu for source 1). If the
FableMenu source is unavailable, sources 2 and 3 still produce a usable DB.
"""
import re, os, sys, glob

FM = sys.argv[1] if len(sys.argv) > 1 else ""   # FableMenu source root (optional)
# egomp repo root: arg 2, else two levels up from this Tools/ script.
REPO = sys.argv[2] if len(sys.argv) > 2 else os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
out_path = os.path.join(REPO, "docs", "data", "symbols.tsv")
if FM and not os.path.isdir(FM):
    print("note: FableMenu source '%s' not found — function names skipped, "
          "regenerating from RTTI + statics only." % FM)
    FM = ""

rows = {}  # addr(int) -> (name, kind, conv, source); first writer wins per addr

def add(addr, name, kind, conv, source):
    a = addr & 0xFFFFFFFF
    if a in rows:
        # Prefer a named func over a bare vtable/data entry; keep existing name.
        return
    rows[a] = (name, kind, conv, source)

# --- 1. FableMenu CallMethod/CallFunction scrape ------------------------------
# Template forms (from core.h):
#   CallMethod<0xADDR, This>(this, ...)              void thiscall
#   CallMethodAndReturn<Ret, 0xADDR, This>(this,...) ret thiscall
#   CallFunction<0xADDR, ...> / ...AndReturn<Ret,0xADDR,...>  cdecl
#   CallFastcall.../CallStdcall... analogues
# Actual template names in core.h: Call / CallAndReturn (cdecl),
# STDCall* (stdcall), FASTCall* (fastcall), CallMethod* (thiscall).
call_re = re.compile(
    r"\b(CallMethod|STDCall|FASTCall|Call)(AndReturn)?\s*<\s*([^>]+?)\s*>",
    re.S)
addr_re = re.compile(r"0x0?0?([0-9A-Fa-f]{5,8})")
# Enclosing function definition — both Class::Method and free functions, with
# an optional calling-convention keyword:  RetType [__conv] Name(args) {
func_def_re = re.compile(
    r"\b([A-Za-z_][\w:*&<> ]*?)\b(?:__thiscall|__fastcall|__cdecl|__stdcall)?\s*"
    r"((?:[A-Za-z_]\w*::)?~?[A-Za-z_]\w*)\s*\([^;{]*\)\s*\{")

conv_map = {"CallMethod": "thiscall", "Call": "cdecl",
            "FASTCall": "fastcall", "STDCall": "stdcall"}

fm_cpps = glob.glob(os.path.join(FM, "**", "*.cpp"), recursive=True) if FM else []
for path in fm_cpps:
    if "imgui" in path.lower() or "minhook" in path.lower():
        continue
    src = open(path, encoding="utf-8", errors="replace").read()
    # Index enclosing function name by character offset (skip control-flow
    # keywords that look like "name(" so they don't shadow the real function).
    KW = {"if", "for", "while", "switch", "return", "else", "do", "catch",
          "sizeof", "case"}
    defs = [(m.start(), m.group(2)) for m in func_def_re.finditer(src)
            if m.group(2).split("::")[-1] not in KW]
    def enclosing(pos):
        name = None
        for start, qn in defs:
            if start <= pos:
                name = qn
            else:
                break
        return name
    for m in call_re.finditer(src):
        kind_word, _ret, targs = m.group(1), m.group(2), m.group(3)
        am = addr_re.search(targs)
        if not am:
            continue
        addr = int(am.group(1), 16)
        if addr < 0x400000 or addr > 0x1400000:
            continue
        qn = enclosing(m.start()) or "sub_%08X" % addr
        add(addr, qn, "func", conv_map.get(kind_word, "-"), "FableMenu")

# Also catch raw reinterpret_cast<...>(0xADDR) call sites with a nearby name.
raw_re = re.compile(r"reinterpret_cast<[^>]+>\s*\(\s*0x0?0?([0-9A-Fa-f]{5,8})\s*\)")
for path in fm_cpps:
    if "imgui" in path.lower() or "minhook" in path.lower():
        continue
    src = open(path, encoding="utf-8", errors="replace").read()
    defs = [(mm.start(), mm.group(2)) for mm in func_def_re.finditer(src)]
    for m in raw_re.finditer(src):
        addr = int(m.group(1), 16)
        if addr < 0x400000 or addr > 0x1400000:
            continue
        qn = None
        for start, q in defs:
            if start <= m.start():
                qn = q
            else:
                break
        add(addr, qn or ("sub_%08X" % addr), "func", "-", "FableMenu")

n_func = len(rows)

# --- 2. RTTI vtables ----------------------------------------------------------
rtti = os.path.join(REPO, "docs", "data", "rtti-classes.tsv")
if os.path.exists(rtti):
    for line in open(rtti, encoding="utf-8"):
        if line.startswith(("#", "VFT")):
            continue
        c = line.rstrip("\n").split("\t")
        if len(c) >= 3:
            add(int(c[0], 16), c[2] + "::vftable", "vtable", "-", "RTTI")

n_vtable = len(rows) - n_func

# --- 3. Curated statics/singletons -------------------------------------------
STATICS = [
    (0x013B86A0, "CMainGameComponent::instance"),
    (0x013B879C, "CGameDefinitionManager::instance"),
    (0x013B89FC, "CQuestManager::instance"),
    (0x013B8A1C, "CThingManager::instance"),
    (0x013B8790, "CHUD::instance"),
    (0x013B7D4C, "CUserProfileManager::instance"),
    (0x013B83D0, "CGame::instance"),
    (0x013BCA10, "GameDirectory"),
    (0x013B8650, "GOverridePlayerStartPos"),
    (0x00BFEA1A, "GameMalloc"),
]
for addr, name in STATICS:
    add(addr, name, "data", "-", "curated")

n_data = len(rows) - n_func - n_vtable

os.makedirs(os.path.dirname(out_path), exist_ok=True)
ordered = sorted(rows.items())
with open(out_path, "w", encoding="utf-8") as f:
    f.write("# Consolidated Fable.exe symbol database (Steam TLC, base 0x400000)\n")
    f.write("# Sources: FableMenu source (func addresses+names), RTTI dump "
            "(vtables), curated statics.\n")
    f.write("# %d symbols: %d func, %d vtable, %d data. "
            "Columns below.\n" % (len(ordered), n_func, n_vtable, n_data))
    f.write("addr\tname\tkind\tconv\tsource\n")
    for addr, (name, kind, conv, source) in ordered:
        f.write("%08X\t%s\t%s\t%s\t%s\n" % (addr, name, kind, conv, source))

print("symbols: %d (func %d, vtable %d, data %d) -> %s"
      % (len(ordered), n_func, n_vtable, n_data, out_path))
# sample of named funcs
named = [(a, r) for a, r in ordered if r[1] == "func" and not r[0].startswith("sub_")]
print("named functions:", len(named))
for a, r in named[:12]:
    print("  %08X  %s  (%s)" % (a, r[0], r[3]))
