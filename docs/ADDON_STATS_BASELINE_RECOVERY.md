# STATS baseline source recovery (no deploy)

> **Goal:** Recover sources that built / match live golden `ijl15.dll` SHA **`4AD0AA7A…`** (`RESTORED_ADDON_STATS_UNEQUIP` / internal `ADDON_UNEQUIP_SYNC_20260802`, size **1438208**) so future patches can be applied on an enter-known baseline.  
> **This doc does not authorize deploy.** Client_1 stays on the bak until a **new feature** build is intentionally shipped.

---

## 1. Golden artifact (truth)

| Field | Value |
|-------|-------|
| Live / bak | Client_1 `ijl15.dll` = `ijl15.dll.bak_ADDON_STATS_UNEQUIP_20260802` |
| SHA256 | `4AD0AA7A93947D797A636F243A104436CA7870495B9B2849E5F7D4A5DEFB4B4E` |
| Size | `1438208` |
| mtime (bak) | 2026-08-02 **13:45:03** |
| Internal stamps | `ADDON_UNEQUIP_SYNC_20260802`, also `ADDON_UI_RESTORE_20260801` |
| Enter | **Proven OK** — do **not** ask user to re-test enter after restore |

### Fingerprint strings (must reappear in any “STATS-identical” rebuild)

| Area | String / behavior |
|------|-------------------|
| Init banner | `main red9/10=BP33+BP10 park54/55 every tick` |
| Pocket wire log | `WireMainPocketSubSlots: red9 BP33 pocket (104,200) + red10 BP10 Si (137,200)` |
| Equip close | `destroy Addon layer` (**not** `+ pocket paint layers`) |
| UNEQUIP | `bag+clear` (`UNEQUIP ok via=%s … (bag+clear)`) |
| ExtEquip | **Absent** — no `ExtendedBodyPart.h` in that build |

---

## 2. What is *not* the golden baseline

| Artifact | Size | Why reject |
|----------|-----:|------------|
| `_build_stats_unequip.bat` deploy @13:50 | **1448448** | Log stamp `ADDON_STATS_UNEQUIP` but **≠** bak size/SHA; matches `bak_ADDON_SHIELD_SI_SPLIT_20260802` mtime |
| Pre-recovery tree `equipaddon.cpp` | — | had ExtEquip / STRICT_WIRE / clear-only / pocket paint (see §6 — now surgically reverted) |
| `Release_frozen_NOHOOK` | objs @15:51 | README = NOHOOK 07-31; objs overwritten by **FULL_FIX** era — polluted |
| WIREOFF / NOPAINT / POCKET_PAINT / FULL_FIX / STRICT_WIRE / REGISTRY_P12 | larger | Enter-unsafe (see avoid-list) |
| Git history | — | **No commit** freezes `ADDON_UNEQUIP_SYNC` / equipaddon STATS; module mostly untracked WIP |

---

## 3. Evidence sources to use

1. **Binary:** Client_1 bak (+ `aside_before_WIREOFF` / `aside_before_ADDON_REGISTRY_P12` / `bak_ADDON_FULL_FIX_…_prev` — all 1438208 @13:45).
2. **Strings:** ASCII scrape as in §1.
3. **Disasm Init:** load bak in IDA or use dumpbin / strings; compare EquipAddon Init path to current `equipaddon.cpp` Init (`park every tick` vs gated wire).
4. **Build script archaeology:** `_build_stats_unequip.bat` shows **which TUs** were rebuilt (`shoulders` / `pendant2` / `equipaddon` / `EquipAddonBridge`) and INC **without** `extendequip` / `bootlog` — use as compile recipe, **not** as proof of output SHA.
5. **Notes:** `ijl15_ADDON_RULES_STATS_*.txt`, `ijl15_ADDON_SI_STATS_*.txt` for product rules that STATS inherited; avoid-list deltas WIREOFF-vs-STATS.

---

## 4. Concrete next coding steps (engineer-only)

**Do not deploy after any of these until size/strings match §1 and a feature build is intentional.**

### Step A — Freeze golden copies

- Keep `bak_ADDON_STATS_UNEQUIP_20260802` read-only.
- Optionally copy to `E:\pro\BeiDou-ijl15\golden\ijl15.STATS_UNEQUIP_4AD0AA7A.dll` for repo-local reference (optional; no Client_1 overwrite).

### Step B — Diff current source → STATS behavior checklist

Edit working tree toward:

| Check | STATS target | Current polluted |
|------:|--------------|------------------|
| ExtEquip include | **Remove** from enter path TUs | `#include ExtendedBodyPart.h` |
| Equip-open XY | Every-tick park red9/10 (STATS Init) | Flag-gated `kEnableEquipOpenXyWire` |
| Pocket paint layers | **None** | destroy `Addon + pocket paint` |
| UNEQUIP | `bag+clear` | `clear-only, server bag` |
| Stamp | `ADDON_UNEQUIP_SYNC_…` or version-only bump | STRICT_WIRE / WIREOFF stamps |

Prefer surgical reverts of REGISTRY/P12/STRICT_WIRE hunks over “flags false + relink Release\*.obj”.

### Step C — Clean rebuild recipe

1. Clean or quarantine polluted `ezorsia\Release\equipaddon*.obj` / `shoulders.obj` / `pendant2.obj`.
2. Compile with INC set like `_build_stats_unequip.bat` (**no** `extendequip` until after baseline match).
3. Link against known-good non-equip objs (from a tree that produced ≤1438208-class builds — verify with strings, not filename).
4. Success criteria **before any Client_1 copy:**
   - Size ≈ **1438208** (± reloc noise only if unavoidable; prefer exact)
   - All §1 fingerprint strings present
   - Absent: `pocket paint layers`, `clear-only, server bag`, `ExtendedBodyPart`, WIREOFF/NOPAINT banners

### Step D — Optional binary-aided recovery

If source surgery stalls:

1. IDA-load golden DLL; name exports / locate Init logging format strings → xref to code.
2. Compare against current `.obj` / PDB of STRICT_WIRE to list **functions that must shrink/vanish** (pocket paint, REMAP, ExtEquip XY table consumers).
3. Recreate those functions from STATS-era comments in avoid-list / architecture docs.

### Step E — When (and only when) to ask user to enter-test

- **Restore bak → live:** no enter A/B request.
- **Rebuild that claims STATS-identical:** engineer verifies SHA/size/strings first; user enter-test **only if** this rebuild is being promoted as a **new live stamp** (even “version-only”).
- **New feature on recovered baseline:** user enter-test required once for that feature stamp.

---

## 5. Process reminder

See [`ADDON_ENTER_CRASH_AVOID.md`](./ADDON_ENTER_CRASH_AVOID.md) § Process:

- STATS bak restore = enter assumed OK.
- WIREOFF / NOPAINT / POCKET_PAINT / FULL_FIX = enter-unsafe forever.
- No flag-only “STATS-identical” claims when size ≠ 1438208.

---

## 6. Recovery pass status (2026-08-02 ~19:40) — engineer staged, **no deploy**

### Staged rebuild

| Field | Value |
|-------|-------|
| Script | `_build_stats_baseline_recovered.bat` (**no** `_deploy_ijl15.ps1`) |
| Staged path | `E:\pro\BeiDou-ijl15\golden\ijl15.ADDON_STATS_BASELINE_RECOVERED_20260802.dll` |
| Staged size / SHA256 | **1445376** / `13D6670F604E6D5640A8DE84D314B6672B27AFF4364E3D53096F718481EB1CEA` |
| Golden size / SHA256 | **1438208** / `4AD0AA7A…DEFB4B4E` |
| Size delta | **+7168** (not yet ≈ match) |
| Internal stamp | still `ADDON_UNEQUIP_SYNC_20260802` (do **not** bump to `BASELINE_RECOVERED` until size≈1438208) |
| Client_1 | **untouched** — remains golden bak @13:45 |

### Restored (string / source checklist)

| Check | Status |
|------:|--------|
| Init `park54/55 every tick` | **OK** in staged DLL |
| `WireMainPocketSubSlots: red9 BP33… + red10 BP10 Si…` | **OK** (every-tick wire; no `WirePocketRed10` / ExtEquip table) |
| `destroy Addon layer` | **OK** — pocket paint destroy path removed |
| UNEQUIP `(bag+clear)` | **OK** — restored SehSetBagItem + ClearSidecar (clear-only removed) |
| No `ExtendedBodyPart` / no ExtEquip include on enter TUs | **OK** (`equipaddon` / `shoulders`) |
| No `POCKET layer` / `pocket paint layers` / SetItem REMAP strings | **OK** |
| No STRICT_WIRE / WIREOFF stamps in exports | **OK** (`Shoulder_GetStamp` / `Pendant2_GetStamp` / `kStamp`) |
| Golden freeze copy | `golden\ijl15.STATS_UNEQUIP_4AD0AA7A.dll` |

### Still divergent

| Gap | Notes |
|-----|-------|
| Size **+7168** vs golden | Fingerprints match; PE not SHA-identical. Likely leftover post-STATS code mass in `equipaddon` (live-UI helpers etc.) and/or `BootLog.obj` rebuilt @15:51 FULL_FIX vs @13:45 link set. |
| Exact byte-identical Rebuild | No git freeze of STATS `equipaddon.cpp`; further shrink needs Step D (IDA golden vs current PDB) or quarantine of post-13:45 non-equip objs. |
| `Release_frozen_NOHOOK` | Still FULL_FIX-polluted — do not trust for baseline objs. |

### Next coding step (still no deploy / no enter ask)

1. Close size gap (Step D binary-aided: drop unused helpers / restore pre-15:51 BootLog.obj if proven smaller).
2. When size≈1438208 **and** §1 strings hold → optionally bump internal stamp to `ADDON_STATS_BASELINE_RECOVERED_20260802`, restage.
3. **Only after user asks:** promote a **new feature** stamp on that baseline and then request enter test once.

