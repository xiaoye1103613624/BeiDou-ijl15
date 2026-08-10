# IDA brief — SetItem first-gate miss on COMPOSE (2026-08-08)

## IDB / deploy status (2026-08-08 ~14:56)

| Channel | Module | Verdict |
|---------|--------|---------|
| Cursor `user-ida-pro-mcp` | **`v265.3.exe`** | **WRONG — ignore**; do **not** ask user to switch |
| HTTP `127.0.0.1:13337` | **`BeiDou.exe`** (`BeiDou_GMS_083.exe.i64`) | **OK** (v083) — optional |
| ijl15 | **file ExpectBytes** | SetItem `@0x3733E` verified on disk |

| Deploy | SHA8 | Notes |
|--------|------|--------|
| **Golden** | **F3F8D0F2** | `golden\ijl15.ADDON_SETITEM_FG_20260808.F3F8D0F2.dll` |
| **Client_1 live** | **F3F8D0F2** | **Deployed 14:56** via bak→overwrite (not CD64_TEST) |
| bak | `Client_1\ijl15.dll.bak_before_SETITEM_FG_20260808_145627` | pre **AEE21638** |
| EXE | **ACF10F63** | untouched |

**Policy (current):** [`NATIVE_EXTEND_PLAN_CLIENT1_20260807.md`](./NATIVE_EXTEND_PLAN_CLIENT1_20260807.md) — **Client_1-direct**; `BeiDou-Client_CD64_TEST` is **reference only**, **not** a required stage gate. SOP still: bak + ExpectBytes + cold select→enter A/B; red → restore bak.

**Next (user):** cold-kill all BeiDou → start **only** `BeiDou-Client_1` → select→enter A/B. If red: restore bak AEE21638.

## Live evidence (pre-deploy AEE21638)

| Item | Value |
|------|--------|
| Client_1 ijl15 (before) | **AEE21638** `ADDON_COMPOSE_APPEND54_TSEC_20260807` |
| Size / stub | 1447424 / `53 E8` |
| EXE | **ACF10F63** untouched |

### Bytes (file offs)

| Site | Live AEE21638 (pre) | FG F3F8D0F2 (now) |
|------|---------------------|-------------------|
| SetItem **first** `@0x3733E` | `8D 43 3D 83 F8 05` (−61..−56) | `8D 43 3E 83 F8 08` (−62..−54) |
| SetItem cash `@0x3734C` | `…A1… 05` | `…A2… 08` |
| SehGet cash `@0x36C6F` | `…A1… 06` | `…A2… 08` |
| SetItem **second** `@0x37370` | `8D 46 CA 83 F8 08` | keep |
| GetItem `@0x34825` | `3E/08` | keep |
| PreferSend `@0x37862` | `CA/08` | keep |
| Apply min `@0x33480` | `83 FE 36` | keep |
| EnsureLayer `@34CE0` | `55 8B EC 6A FF` | keep (ExtraRing / Addon dock) |
| OriginJe `@5911A` | `90×6` | keep |

### Runtime log (`equipaddon_debug.log` ~23:38, pre-FG)

- Init still prints `BP56-61` / stamp `ADDON_ROW3_UNEQUIP_UI_20260802` (string not updated by COMPOSE).
- `Sidecar SetItem` for −61/−60/−59 only.
- Totem dblclick all `-> bp=55`; wear −54/−55/−62 **no** SetItem.
- Matches avoid **#59** exactly.

## Root cause

`tools/patch_client1_compose_append54_62_tsec_20260807.ps1` widened the **second** SetItem IsSidecar gate (`@0x37370`) but never the **first** range check (`@0x3733E`). Badge/totem1/aux fall through → empty sidecar → blank UI + multi-wear UX. `FindFreeTotemBp` always returns 55.

## Fix script

`tools/patch_client1_compose_setitem_firstgate_20260808.ps1`

- Default: write **golden** only.
- `-DeployLive` → bak + overwrite **Client_1** (canonical path per NATIVE_EXTEND_PLAN; **not** CD64_TEST).

## Server companion (running pid 3488 @14:27)

- `ItemFactory` 64-col INSERT + gem/chaos/reforge load: **in** `target\classes` (mtime 13:13) **and** JVM started after → **loaded**.
- `ExtendedEquipRegistry.resolveTotemDst` — fill empty −55…−58; **in** running classpath (class mtime 11:54 < start 14:27).
