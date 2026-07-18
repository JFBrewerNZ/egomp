# Tools

Reverse-engineering and dev-loop tooling for EgoMP. Python scripts need
`pip install pefile capstone` (and `lief` only for the Mac extractor).

## Dev loop

| Tool | What |
|---|---|
| **`deploy.ps1`** | One command for the whole build+deploy cycle: closes running clients (polls until the DLL handle is released — a hung client can hold it several seconds), builds `Core\Core.vcxproj` Release\|Win32, copies the DLL to `egomp\EgoMP.dll` and `egomp\Release\`. `-Launch N` starts N clients after; `-KeepClients` skips the close step. Replaces the manual msbuild + copy dance and the stale post-build copy that always "fails". |
| **`symbolize.py`** | Turn a CrashDiag `EgoMP-crash-<pid>.log` into named frames: resolves every `Fable.exe+0xOFFSET` to the nearest symbol in `docs/data/symbols.tsv`. Honestly flags frames in unmapped regions (large delta) instead of guessing. Also `symbolize.py 0xADDR` for a one-off. **Run this on every crash log first.** |

```powershell
# typical iteration
.\Tools\deploy.ps1 -Launch 2
# after a crash
python Tools\symbolize.py egomp\EgoMP-crash-25580.log
```

## Reverse engineering

| Tool | What |
|---|---|
| **`build_symbols.py`** | Regenerates `docs/data/symbols.tsv` (2350 symbols) by merging FableMenu's named function addresses, the RTTI vtable table, and curated statics. The committed TSV is the artifact; re-run only to refresh it (`python build_symbols.py <FableMenu-src> [repo]`; works without FableMenu, just fewer names). |
| **`vtable_probe.py`** | Dump any class's vtable from the live exe and gauge real-vs-stub methods + winsock/import calls. `python vtable_probe.py CFoo=0x01234567`. Look up vtables in `docs/data/rtti-classes.tsv`. This produced the dormant-netcode finding. |
| **`rtti_scan.py`** | Walk MSVC RTTI to dump a class's vtable + virtual function addresses, and find code that references given strings. |
| **`callgraph.py`** | Capstone function-boundary + call-target dump around an address. |
| **`anim_scan.py` / `anim_disasm.py` / `anim_vfuncs.py`** | The animation-system RE scripts (see RE-NOTES.md). |
| **`extract_mac_symbols.py`** | LIEF-based Mach-O symbol dumper for the Feral Mac binary *if a legitimate copy is obtained* — emits demangled symbols + a cross-reference against `rtti-classes.tsv`. See `docs/community-resources.md`. |

## Notes & data

- **`RE-NOTES.md`** — first-party RE findings (equipment/inventory/animation/
  input systems).
- Consolidated third-party knowledge and reference data live in
  [`../docs/`](../docs/README.md), including `docs/data/symbols.tsv` (what
  `symbolize.py` reads) and `docs/data/rtti-classes.tsv`.
