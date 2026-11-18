# tools/re — reverse engineering the running game

Read-only tools: the game is never written to, paused or attached to, so they
are safe while someone plays. Findings go to
`docs/research/engine-knowledge.md` §13.

| Tool                                 | What                                                                               |
| ------------------------------------ | ---------------------------------------------------------------------------------- |
| `run SCRIPT ARGS…`                   | Python with capstone, pefile and numpy (nix-shell), `tools/re` on the path         |
| `mem.py`                             | `Mem` (live memory over ssh, through `memsrv`), `Exe` (the exe by address, xrefs)  |
| `memsrv/`                            | the reader on the game machine: `/proc/PID/mem`, pattern and pointer scans (Go)    |
| `rtti_dump.py`                       | live RTTI → `.cache/re/rtti.json`, `names.txt` (20 s, the game must be running)    |
| `rtti.py class\|find\|func\|vtable…` | query the dump; `export` rewrites `docs/research/data/rtti-*.tsv`                  |
| `live.py player\|entity\|anim\|…`    | live objects: V's components, an entity's animation objects, instances of a class  |
| `record_pose.py [SECONDS] [NAME]`    | V's live pose to `recordings/pose-NAME.{json,bin}` for offline retarget/codec work |
| `pose_codec.py REC [HZ]`             | offline check of the pose stream design on a recording (sizes, errors)             |
| `pose_export.py REC`                 | a recording → `REC.tpose`, which `tandem-peer --pose` plays in a loop              |
| `asm.py ADDR`                        | annotated disassembly of a function                                                |
| `xref.py ADDR`                       | functions that reference an address                                                |
| `addr.py ADDR\|HASH`                 | address ↔ address-library hash                                                     |
| `decompile [+N] ADDR…`               | Ghidra's C for functions (and callees N levels down); first run imports the exe    |

Setup, once: copy `bin/x64/Cyberpunk2077.exe` and
`bin/x64/cyberpunk2077_addresses.json` from the game machine to `.cache/re/`.
`memsrv` is built and copied to `/tmp/tandem-re/` on the game machine by
`mem.py`. The Ghidra project lives in `build/ghidra` (Ghidra refuses paths
with a dotted element).

Notes:

- The exe loads at its preferred base under Proton: live addresses in the
  image equal the file's. Heap addresses change at every launch.
- Live reads are not synchronized with the game: a structure can be read
  mid-update. Read twice when it matters.
- `Exe.xrefs` is a byte scan for rip-relative displacements: virtual calls
  are not found; look for the vtable slot instead (`rtti.py vtable`).
