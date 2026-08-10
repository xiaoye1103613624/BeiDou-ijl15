# IDA brief — ring tip/unequip/hat-drop + FA restore (2026-08-07)

## IDA

- MCP: `http://127.0.0.1:13337/mcp` (Cursor `user-ida-pro-mcp` discovery broken; HTTP OK)
- IDB: `BeiDou_GMS_083.exe.i64` · module `BeiDou.exe` · imagebase `0x400000` · hexrays ready

## HitTest / bodypart (ring→hat)

| Site | Finding |
|------|---------|
| `sub_7FEC32` GetBodyPartFromPoint | Walks `ecx=&dword_BE2264` … end `cmp ecx, BE23F4`; returns `index+1` as BP |
| ExpectBytes `@7FEC32` | `33 D2 B9 64 22 BE 00` … `@7FEC6F` `81 F9 F4 23 BE 00` |
| Vanilla end | **BP1–50 only**; cash HT starts `@BE23F0` (hat cash `(38,35)` same as BP1) |
| Writing PE HT index≥50 | Smashes cash hat/face → dual tip / “hat moved” (equip-slot-lessons) |
| ExtraRing | DLL `g_hitTestExt[60]` + rewrite imm `@7FEC35` + end `@7FEC6F+2`; BP52/53 @ `(104,35)/(137,35)` |

Callers of `7FEC32`: tip `sub_7FE9BB@7FE9FC`, drag `sub_4F19CD` / `sub_4F4BB7`, hover draw `sub_7FE4C6`.

Unequip dblclick path still uses wear occupancy @`4F0B89` (shadow GetItem for −52/−53).

## LIVE vs FA (byte diff = 9)

| RVA | FIX3 `514D9D68` | FA `FA26021C` |
|-----|-----------------|---------------|
| `EnsureLayer@34CE0` | `33 C0 C3` (overlay OFF) | `55 8B EC 6A FF` (Layer ON) |
| `OriginJe@5911A` | `90×6` (force UpdateResolution) | `0F 84 A3 02 00 00` |

No other PE differences. Ring HT/Get/Set code identical; Origin NOP + Layer OFF explain: no 扩展栏; layout/HT mismatch → tip/unequip dead + drop→hat.

## Deploy

Restored Client_1 → **`FA26021C`** (known enter-green PEER_ASLR_V2). Bak FIX3 kept. No new caves / no growth / stub `53 E8` intact.
