# IDA brief — COMPOSE Layer ON + Origin NOP (2026-08-07)



## Mandate



Fix forward (not pure FA rollback, not FIX3 Layer-off). Dual: UI flush-bottom **and** 扩展栏 + −52/−53 tip/unequip/drag.



## IDA / evidence status



| Target | Status |

|--------|--------|

| Cursor MCP `user-ida-pro-mcp` | Auth OK but **wrong IDB loaded**: `v265.3.exe` — **not** BeiDou.exe / ijl15 |

| BeiDou `sub_7FEC32` / tip `7FE9BB` / drag `4F19CD`/`4F4BB7` | Reused prior brief + **on-disk EXE bytes** (ACF10F63) |

| ijl15 `EnsureLayer@34CE0` / `OriginJe@5911A` | PE RVA ExpectBytes + FA↔FIX3 2-site diff |



**Blocked for fresh Hex-Rays:** load `BeiDou_GMS_083.exe.i64` (or Client_1 `BeiDou.exe`) in IDA before any further EXE cave work.



## EXE HitTest (unchanged this stamp)



| Site | ExpectBytes / finding |

|------|------------------------|

| `GetBodyPartFromPoint@7FEC32` | `33 D2 B9 64 22 BE 00` … end `@7FEC6F` `81 F9 F4 23 BE 00` |

| Walk | BP1–50 only in PE; cash HT @`BE23F0` |

| ExtraRing | DLL `g_hitTestExt[60]` + rewrite imm `@7FEC35` / end `@7FEC6F+2`; BP52/53 `(104,35)/(137,35)` |

| Forbidden | Smash PE HT index≥50 (cash hat dual-tip / drag→hat) |



Callers: tip `7FE9BB@7FE9FC`, drag `4F19CD` / `4F4BB7`, hover `7FE4C6`.



## ijl15 sites (this compose)



| RVA | FA `FA26021C` | FIX3 `514D9D68` | **COMPOSE** |

|-----|---------------|-----------------|-------------|

| `EnsureLayer@34CE0` | `55 8B EC 6A FF` ON | `33 C0 C3` OFF | **ON** `55 8B EC 6A FF` |

| `OriginJe@5911A` | `0F 84 A3 02 00 00` | `90×6` | **`90×6`** |

| `LogInitOnce@34C80` | `55 8B EC` | `55 8B EC` | keep |

| `hooks@599A0` | `80 …` | `80 …` | keep |

| `stub@AB30C` | `53 E8` | `53 E8` | keep |



### Origin je interaction



`rs_afterSuccessfulSwitch` @`cmp [ebp+14h],0` then `je field` @`5911A`.  

FA live `rs_ui.log` still shows **`first field Origin install`** with `follow_login=1` `grChanged=0` (binary predates / diverges from source skip).  

NOP je → fall through login `UpdateResolution` path → `rs_originActive=false` → StatusBar/hotkeys flush-bottom.



### Layer vs ExtraRing



- FIX3 Layer OFF → 扩展栏 gone; ring tip/unequip/drag regress (confounded with Origin NOP).

- COMPOSE keeps Layer ON (FA behavior for Addon dock) **and** Origin NOP (UI).

- ExtraRing HT remains shoulders DLL table — no PE cash smash. If tip still wrong under UpdateResolution-only layout, next fix is **slot XY / `Shoulder_SetExtHitTestSlot`**, not dropping Origin NOP.



## Deploy (live)

| Item | Value |
|------|--------|
| Live SHA8 | **`20251F3D`** |
| SHA256 | `20251F3D251386A5B5E84644DDD46C153C183543924FCEDEDE64907107699627` |
| Size / stub | 1447424 / `53 E8` |
| Script | `tools/patch_client1_compose_layer_origin_20260807.ps1` |
| Bak | `ijl15.dll.bak_before_COMPOSE_LAYER_ORIGIN_20260807_*` |
| Golden | `golden/ijl15.ADDON_CLIENT1_COMPOSE_LAYER_ORIGIN_20260807.20251F3D.dll` |
| EXE | `ACF10F63` untouched |



## Retest



1. Cold start → enter A/B green  

2. StatusBar / hotkeys / minimap / banner **not** 偏上  

3. E → 扩展栏可见  

4. −52/−53 tip + unequip + drag to ring (not hat)  

5. Classic seats OK  

