# BeiDou GMS v083 — Extended Equipment Architecture

> **Status:** Live = **pre-SENDBUSY restore** SHA `6D612F01…` (1447424). No user enter retest on bak restore.  
> **Adapt plan (post-SENDBUSY rollback):** [`ADDON_083_ADAPT_FROM_095.md`](./ADDON_083_ADAPT_FROM_095.md) — **主路线：对照 095 原生模型适配 083**  
> **Deep baseline bak:** `ijl15.dll.bak_ADDON_STATS_UNEQUIP_20260802` SHA `4AD0AA7A…` size `1438208`  
> **Enter-unsafe (do not redeploy):** WIREOFF / P12_* / FULL_FIX / **SENDBUSY_FIX** — `0xC0000409`  
> **Canonical avoid-list:** [`ADDON_ENTER_CRASH_AVOID.md`](./ADDON_ENTER_CRASH_AVOID.md) · Client_1 mirror  
> **095 IDA notes:** [`ADDON_095_IDA_BODY_PART_NOTES.md`](./ADDON_095_IDA_BODY_PART_NOTES.md)  
> **STATS source recovery:** [`ADDON_STATS_BASELINE_RECOVERY.md`](./ADDON_STATS_BASELINE_RECOVERY.md)  
> **Addon row3 wear/unequip:** [`ADDON_ROW3_WEAR_UNEQUIP.md`](./ADDON_ROW3_WEAR_UNEQUIP.md) — packet-only（P1 方向）  
> **Prior FIXES bak:** [`ADDON_ROW3_FIXES.md`](./ADDON_ROW3_FIXES.md) — `60E3E02F…`  
> **ROW3 base:** [`ADDON_ROW3_POCKET_SI.md`](./ADDON_ROW3_POCKET_SI.md) — bak `D2C538A9…`

---

## 1. Executive summary

### Product HARD rules (2026-08-04) — Equip display

1. **Do not modify original CUIEquip display logic** — classic seats keep vanilla draw / tip / hover (`sub_7FE9BB`, `sub_8EBA5B`, `CUIToolTip__SetToolTip_Equip`). **Forbidden:** nop dual-tip `jnz` `@7FEA9A` (`ADDON_HOVER_RELOG_NODUAL`).
2. **Addon seats are new**, but must **reuse** native wear / unequip / drag / dblclick / hover-tip / enhance behavior — mirror inventory seats; do not invent parallel tip/draw for classic bodyparts.
3. Tip/draw regressions → fix **Addon-side** or GetItem sidecar (normal-only −56…−61); **never** compensate by patching vanilla tip.

Recommended architecture (≤10 bullets):

1. **One registry, three consumers** — declare each seat once as `ExtendedBodyPart{…}`; server AbleToWear / STAT inject / client Get-Set range / XY park all read the same table.
2. **Reuse vanilla wear path** — bag dblclick / drag → native `SendChangeSlotPosition` / server `InventoryManipulator.equip`; plugin only remaps bodypart + UI hit-rects. **Forbid** parallel wear pipelines. **Same for tip/hover** — reuse native, do not nop dual tip.
3. **Three storage tiers** — (A) native CharData ZRef in apply-max≤55 (shoulder −20, pendant2 −51, classic −1…−50); (B) ExtraRing/Addon Get-Set **shadow** ZRefs (−52…−61) with Occ OFF; (C) equipaddon **sidecar** for BP56–61 login apply (jg-allow, never raise apply-max).
4. **Client-blind seats → server STAT_CHANGED** — any seat with Occ OFF must appear in `Character.isClientBlindEquipSlot`; never Occ-walk bad ZRefs (`@535351` / `@77F99E`).
5. **Enter-safe lifecycle** — attach only park + range hooks at DllMain; GetSlotXY / Si / pocket wire **only while Equip UI open**; pendant2 UI only after field enter.
6. **UI split ≠ storage split** — 109/134/135 share Si −10; pocket 116 owns −33; **target UI** = Addon row3 seats (not main red9/10). **Never** draw-skip punch / BP33 cave.
7. **Cash policy** — no −152/−153↔−52/−53 remap in Get/Set caves; cash rings stay classic −112…−116.
8. **Unequip** — sidecar/Addon clear-only; server grants bag item (no client optimistic bag → dupe class).
9. **Versioning discipline** — stamp string ≠ bak filename; SHA256 is truth; never trust mislabelled bak (`SI_ENTER_SAFE` == `SI_STATS`).
10. **Add seat N = table row + test matrix** — no new ad-hoc caves; ExtraRing re-enable is a **gated bisect**, not a drive-by flag flip.

---

## 2. What already works (correct route)

### 2.1 Pattern library — learn from these three

| Pattern | Item → slot | Why it works | Do **not** copy blindly |
|---------|-------------|--------------|-------------------------|
| **护肩 (shoulder)** | 115 → BP20 / −20 | Native-range seat (≤55 apply-max); combat skip NOP for −20; Main Equip red-8 XY/HT; stats via native walker after NOP | Do not rebind to 095’s BP51; do not OccShadow |
| **第二项链 (pendant2)** | 112 → BP51 / −51 (2nd when −17 full) | Server routes 2nd Pe to −51; UI gated `pendant2_ui` **after CField**; TSec gates NOP mirror of −20; no ForceDrawLoop widen | First pendant2 UI hooks caused AuthSuccess hang — keep post-field only |
| **6戒 ExtraRing shadows** | 111 → −52/−53 (non-cash) | Get/Set address caves + Addon bind chain; **stats via STAT_CHANGED** while Occ OFF | Shadows ON + −152 remap = FULL_FIX `0xC0000409`; keep OFF until enter-green bisect |

### 2.2 Ownership table (live design intent)

Legend: **Wear** = AbleToWear/equip dst · **Disp** = icon/HT · **Stats** = 四维/combat fold · **Persist** = DB EQUIPPED pos

| Slot | Prefix / islot | Wear owner | Display | Stats | Persist | Enter policy |
|-----:|----------------|------------|---------|-------|---------|--------------|
| −20 | 115 `Sh` | Server + native | Main Equip red 8 | Client native (NOP skip) | DB −20 | Always ON |
| −51 | 112 `Pe` (2nd) | Server route when −17 full | Main Equip (pendant2_ui) | Client native (TSec NOP) | DB −51 | UI after field |
| −17 | 112 `Pe` | Vanilla | Main Equip | Vanilla | DB −17 | Vanilla |
| −12/−13/−15/−16 | 111 `Ri` | Vanilla | Main Equip | Vanilla | DB | Vanilla |
| −52/−53 | 111 `Ri` | Server RING allow | Main red 3/4 (when shadows ON) | **Server STAT blind** | DB −52/−53 | **Shadows OFF** on live/FIX2 |
| −54 | 118 `Ba` | Server prefix | Addon dock | Server STAT blind | DB −54 | Park main XY; Addon draw |
| −55…−58 | 120 `To` | Server fill empty | Addon dock | Server STAT blind | DB | Totem×4 hard cap |
| −59 | 119 `Em` | Server (never Si) | Addon dock | Server STAT blind | DB −59 | Emblem ≠ −10 |
| −60 | 166 `Dr` | Server | Addon dock | Server STAT blind | DB −60 | **Not** BP21 (pet) |
| −61 | 167 `Ht` | Server | Addon dock | Server STAT blind | DB −61 | **Not** BP22 (pet) |
| −33 | 116 `Po` | Server | **Addon row3** overlay (not main red9) | Client walker (≤51) — **not** blind | DB −33 | Paint/HT only on Addon while Equip open |
| −10 | 109/134/135 `Si` | Server mutual exclude | 109=vanilla shield; **134/135=Addon row3** | Client native — **never** blind | DB −10 | Addon seat filters 134/135 only |

Cash mirrors: −1xx / −15x…−161 follow same ownership with `cash = slot−100` rule; cash ExtraRing **must not** use −152/−153.

### 2.3 Shadow vs Occ vs Sidecar (mental model)

```text
                    ┌─────────────────────────────────────────┐
                    │  apply-max ≤ 55  (NEVER raise to 59)    │
                    │  native aEquipped[1..55] ZRef           │
                    └───────────────┬─────────────────────────┘
                                    │
         ┌──────────────────────────┼──────────────────────────┐
         ▼                          ▼                          ▼
   Native seats              Get/Set shadow               Sidecar (DLL)
   −1..−51 (−20,−51)         −52..−61 inventory           BP56–61 ZRef[]
   combat NOP / TSec         caves (Addon always;         login lea redirect
                             ExtraRing gated)             jg-allow 56–61
         │                          │                          │
         ▼                          ▼                          ▼
   Client folds stats        Occ OFF → STAT_CHANGED       GetItem/SetItem hook
                             inject blind flats           + Addon UI seats
```

---

## 3. Layering model

### 3.1 Server (`gms-server`)

| Concern | Today | Target |
|---------|-------|--------|
| Slot registry | `EquipSlot` enum + prefix if-else in `InventoryManipulator.equip` | Keep enum; extract **prefix→dst policy** into one `ExtendedEquipRegistry` used by equip + AbleToWear + blind list |
| AbleToWear | `ItemInformationProvider` + `EquipSlot.isAllowed` | Registry-driven; prefix-first (119 never Si; 116 before islot) |
| Persist | EQUIPPED inventory positions as today | Unchanged semantics |
| modifyInventory / login encode | `PacketCreator` special-cases −52…−61 | Generated from registry `encodePolicy` |
| Stats for blind seats | `Character.isClientBlindEquipSlot` + `computeClientDisplayBaseFourStats` | Registry flag `clientBlind=true` → single list |
| Combat / recalc | Full EQUIPPED walk (already correct for −20/−51/Addon) | Keep; do not filter by “client can see” |

**Hard server rules already proven:**

- Prefix-first routing (116/119/166/167 before WZ islot).
- Cash rings: never land −152/−153.
- Pendant2: route second Pe to −51 without double remove/add.
- Totem: exactly 4 slots; refuse 5th.
- Emblem 119 → −59 only.
- Android/Heart → −60/−61 (not pet −21/−22).

### 3.2 Client plugin (`BeiDou-ijl15`)

Thin hooks only:

| Module | Responsibility | Forbidden |
|--------|----------------|-----------|
| `shoulders/` | Bodypart accept; Get/Set range caves; combat/TSec NOPs; HT table for main extended seats; ExtraRing **flags** | OccShadow; apply-max>55; early-return entire ExtraRing bind |
| `pendant2/` | Post-field XY/HT for BP51 | ForceDrawLoop widen; DllMain-always UI |
| `equipaddon/` | Addon dock UI; sidecar Get/Set BP56–61; Equip-open pocket/red10 wire; clear-only unequip | BP33 draw cave; UIToggle/OnDraw detours; optimistic bag |
| `compat/LazyCompatInit` | Field-enter + tick orchestration | Touching CharData before stage |

**Forbid inventing parallel wear pipelines** — wear/unequip always ends in `SendChangeSlotPosition` / server equip.

### 3.3 UI surfaces

| Surface | Seats | Mechanism |
|---------|-------|-----------|
| Main Equip (vanilla) | Classic −1…−19, −49/−50, shield draw, rings 1–4 | Untouched PE tables |
| Main Equip (remapped) | Shoulder red8, pendant2, ExtraRing red3/4 | GetSlotXY / HitTest / BE27 park or remap — **no** pocket/aux red9/10 |
| Addon dock (sidecar panel) | Totem×4, Emblem, Android, Heart, Badge, **Pocket, Aux(134/135)** | Overlay layer; park main BP21/22/54/55 off-panel; row3 = −33 / −10 UI |
| Vanilla shield icon | 109 only | Native Si draw path |

---

## 4. Extension point API (add seat N without new caves)

### 4.1 Canonical record

```cpp
// Design sketch — single source of truth (C++ header + mirrored Java constants)
enum class UiSeat { MainEquip, AddonDock, HiddenPark };
enum class DrawPolicy { Vanilla, RemapXY, AddonOverlay, NeverDrawEnter };
enum class StorageTier { NativeApplyMax55, ShadowGetSet, SidecarZRef };

struct ExtendedBodyPart {
    int           bp;              // positive bodypart (== -slot)
    int           itemPrefix[];    // e.g. {115}, {116}, {111} with multi-slot
    const char*   islot;           // WZ code; may be empty if prefix-only
    UiSeat        uiSeat;
    StorageTier   storage;
    bool          clientBlind;     // → STAT_CHANGED inject
    DrawPolicy    drawPolicy;
    bool          enterSafe;       // false ⇒ Equip-open or post-field only
    int           xyX, xyY;        // main remap / park target
    int           cashAlias;       // 0 or -(bp+100)
    const char*   notes;
};
```

### 4.2 Registration-driven surfaces (one of each)

| Surface | Implementation rule |
|---------|---------------------|
| **Get/Set range** | One cave: `if (slot in registry.shadowOrSidecarRange) → DLL ZRef` — widen by updating min/max from table, not new caves |
| **XY / HitTest remap** | One table `g_xyRemap[bp]`; tick applies only rows with `enterSafe || EquipOpen` |
| **Server inject list** | `isClientBlindEquipSlot` generated from `clientBlind` |
| **AbleToWear / dst** | `resolveDst(itemId, suggestedDst)` from prefix + multi-slot fill policy |
| **Addon seats** | `kSeats[]` becomes filtered view of `uiSeat==AddonDock` |

### 4.3 Adding seat N — checklist (no new cave class)

1. Append row to registry (both Java + C++ stamp sync).
2. Choose **StorageTier** by apply-max / pet-collision rules (see §6–7).
3. If `clientBlind`: add to STAT inject (automatic if generated).
4. If `enterSafe=false`: wire only in Equip-open / post-field path.
5. Run **test matrix §9** for that seat alone, then full matrix.
6. Ship under new VERSION stamp + SHA; never overwrite live without bak.

---

## 5. Enter-safe lifecycle

```text
[DllMain]
  ✓ park Addon BPs off-panel (−2000)
  ✓ install Get/Set range caves (shadow/sidecar) — no CharData walk
  ✓ login jg-allow 56–61 + lea redirect (sidecar)
  ✗ NO GetSlotXY mutation
  ✗ NO ExtraRing shadows (until bisect green)
  ✗ NO OccShadow / apply-max raise / Equip UIToggle

[Select char → Enter field]
  ✓ native decode apply-max≤55
  ✓ sidecar absorb BP56–61 via lea caves
  ✗ NO pocket/Si XY wire
  ✗ NO pendant2 ForceDrawLoop

[CField ready / LazyCompat]
  ✓ optional pendant2_ui XY/HT (config)
  ✓ Addon EnsureHooks idle

[Equip UI open]  ← only now
  ✗ NO WireMainPocketSubSlots red9/red10 (abandoned — see ADDON_ROW3_POCKET_SI)
  ✓ Addon dock create/sync/input (incl. row3 pocket + aux seats)
  ✓ hover tip / drag wear / clear-only unequip

[Equip UI close]
  ✓ destroy Addon layer; leave park
```

**Rule of thumb:** if a hook reads `GetItem` / mutates `GetSlotXY` / walks ZRefs for draw, it is **Equip-open or post-field**, never select→enter.

---

## 6. Shield vs Si vs Pocket

### 6.1 Official / product rules (BeiDou 083)

| Item | Storage | UI | Notes |
|------|---------|----|-------|
| 109 shield | **−10** | Vanilla shield seat | Untouched draw path |
| 134 / 135 aux | **−10** (mutex with 109) | **Addon row3**（非红10） | Same −10 storage; Addon HT filters 134/135 only |
| 116 pocket | **−33** (−133 cash) | **Addon row3**（非红9） | Overlay paint via Addon `Paint()`; **no** BP33 draw cave |
| 119 emblem | **−59** | Addon | WZ islot often `Si` — **prefix wins**; never −10 |

Detail + seat coords: [`ADDON_ROW3_POCKET_SI.md`](./ADDON_ROW3_POCKET_SI.md).

### 6.2 Why Addon row3 (UI split, one −10) is enter-safe

- Storage stays vanilla single Si slot → AvatarLook / encode / two-hand unequip stay correct.
- Pocket/aux icons drawn by **Addon overlay** (same path as totem/badge) while Equip open → no widen of `bp>20 && bp<=48` draw-skip.
- Opening draw-skip or BP33-only cave reintroduced `0xC0000409` (ucrtbase GS) — forbidden.
- Abandoning main red9/10 removes Equip-open GetSlotXY mutation for BP33/BP10 (STATS `WireMainPocketSubSlots`) — fewer main-table writers.

### 6.3 What not to do

- Punch draw-skip at `0x007FEE89` (48→32).
- Treat 119 as shield because islot=`Si`.
- Re-enable main red9/10 park as the long-term pocket/aux UI (superseded by Addon row3).
- Accept 109 on Addon aux seat.
- Copy 095’s 135→weapon or 119→BP10.

### 6.4 FAQ — 为什么原红 9 能用，换成口袋 116 就不行？→ 改 Addon 第三行

**结论：** GetSlotXY 重映射本身没坏；坏在 **显示用 bodypart 类别**。口袋 BP33 落在原版图标绘制跳过带里，不能靠改 skip / 开 BP33 cave 进图画。**正解 = Addon overlay（第三行）**，不是主栏红9。

1. **历史红 9** 服务的是 bodypart **本来就在原生绘制范围**（或不走 `bp>20 && bp<=48` 跳过）的座位——典型是徽章/图腾时代槽位。GetSlotXY / park 把坐标指到 `(104,200)` 只改命中与停泊，**不需要**穿透绘制跳过带。
2. **口袋 116 → −33 / BP33** 正好落在原版 Equip 图标绘制跳过：`bp==14 || (bp>20 && bp<=48)`。原生循环 **从不** 画 BP33。强行把 skip 上界 `48→32`，或加 BP33-only draw cave，会在选角→进图、身上已穿 −33 时触发 `0xC0000409`（ucrtbase GS / `__fastfail`）。
3. 所以「换类型不行」≠ XY park / hittest 坏了：同一套坐标对拖拽命中、掉落仍可用；**图标绘制** 对 BP33 必须走 **Addon sidecar Paint**，**禁止** 进图期改 draw-skip。
4. **副手 134|135** 存储仍在 −10；UI 从红10迁到 Addon 第三行，与盾 109 主栏原生位并存（互斥存储，UI 过滤）。
5. **095** 无口袋；副手在主栏 BP10。我们抄的是 Mechanic/Dragon「独立窗」哲学 → BeiDou Addon 面板，而非照搬 BP 数字。

---

## 7. 095 / higher-version learnings

> **Post-rollback execution plan:** after `ADDON_SENDBUSY_FIX` boot crash, do **not** stack more SendBusy/sidecar hacks. Follow [`ADDON_083_ADAPT_FROM_095.md`](./ADDON_083_ADAPT_FROM_095.md) (P0 stable bak → P1 packet-only wear → P2 early login allow → P3 optional CD grow → P4 ExtraRing last).

### 7.0 Process (enter testing)

- Restoring STATS bak = enter assumed OK — **do not** request select→enter A/B.
- Request in-game enter test **only** when intentionally deploying a **new feature stamp**.
- Detail: [`ADDON_ENTER_CRASH_AVOID.md`](./ADDON_ENTER_CRASH_AVOID.md) § Process.

### 7.1 IDA status

| DB | Status |
|----|--------|
| Cursor `user-ida-pro-mcp` `:13338` | **v083** `BeiDou.exe.i64` — draw-skip / GetSlotXY / HitTest |
| 095 MCP `:13339` (2026-08-02 live) | **`GMS_v95.0_U_DEVM.exe`** — bodypart / Draw / HitTest / GetItem re-decompiled |
| GMS 095 IDB | `E:\资料\xiaoye\mxd学习\GMS_v95.0_U_DEVM.i64` |
| Notes | [`ADDON_095_IDA_BODY_PART_NOTES.md`](./ADDON_095_IDA_BODY_PART_NOTES.md) + `萧曳冒险岛\095装备槽评估-20260725.md` |
| TMS120 | Resource/server survey only — no ExtraRing/Pocket/Android in stock 120 |

*Process:* two IDAs → 083 keeps `:13338`, 095 listens `:13339`. Cursor default MCP stays on 083 unless rebound.

### 7.2 What 095 shows (port vs cannot) — live Hex-Rays

| Feature | 095 client (live) | Port to v083? |
|---------|-------------------|---------------|
| Shoulder 115 | **BP51** (`0x46FBE0`) | **No** — keep BeiDou **BP20/−20** |
| Pendant2 | 112 → 17+**59**; HitTest/Draw max **58/59** (`0x7A3CE0` / `0x7AA560`) | **Pattern only** — BeiDou pendant2 idle **BP51/−51**; emblem owns −59 |
| Rings | **4 only** | Extra 5/6 **custom** |
| Draw skip | **Same** `bp==14 \|\| (21…48)` — BP33 skipped even on 095 | **No pocket paint to copy**; sidecar only |
| `sizeof(CharacterData)` | **1965**; Get/Set allow **−59** (`0x42B990`) | **Cannot** raise 083 apply-max / CD without EXE grow |
| 109/119/134 | All BP**10**; **135→weapon BP11** | Keep BeiDou Si−10 mutex; **never** 119→10 or 135→weapon |
| Pocket / Badge / Totem / Android | **Absent** in `get_bodypart` | Addon/sidecar |
| Mechanic 1100+ / Dragon 1000+ | Separate UI (`0x7A3870` / `0x7A38C0`) | Optional later; not Addon dock |
| Android ≤−1200 in 095 zip servers | Private mashup ≠ IDA | Do not copy −1200 protocol |

### 7.3 Higher Maple server patterns worth keeping

- Dedicated enum / slot allow-lists (BeiDou `EquipSlot` already).
- Trust-but-verify dst with server-side prefix routing (don’t trust client islot alone).
- Separate UI windows for job kits (Mechanic/Dragon) — if ever needed, **new overlay**, not smash into apply-max 59.
- Cash = normal − 100 aliasing — keep; never invent −152 ring cash.
- Grow HitTest/Draw ceiling **with** real CharacterData seats (095’s 58/59 + 1965-byte CD) — never punch pet skip alone.

### 7.4 v083 hard cannots

- Native `aEquipped[56+]` inside vanilla apply-max 55 without CD64 EXE project.
- OccShadow over empty/forced ZRefs on enter.
- Treating pet BP21/22 as Android/Heart.
- Assuming 095 shoulder / pendant2 / 119 / 135 numbers equal 083 product seats.
- Assuming 095 Draw will paint pocket BP33 (it will not).

---

## 8. Migration plan (ad-hoc → table-driven)

### 8.1 Current debt

- `shoulders.cpp`: dozens of `kEnable*` flags + duplicated prefix switches.
- `equipaddon.cpp`: `kSeats[]` UI-only; storage/blind/enter flags live elsewhere.
- `InventoryManipulator.equip`: long prefix if-else (correct but not shared with client).
- Mislabelled baks / stamp≠filename incidents.

### 8.2 Phased migration (implementation later — design freeze first)

| Phase | Goal | Enter risk | Exit criteria |
|------:|------|------------|---------------|
| **0** | Doc + registry sketch (this file); live stays STATS_UNEQUIP | None | User accepts architecture |
| **1** | Introduce `ExtendedBodyPart` table in plugin (read-only mirrors flags); no behavior change | None | **Code landed** `extendequip/`; staged build only |
| **2** | Server `ExtendedEquipRegistry` drives AbleToWear + blind list + prefix dst | Low | **Code landed**; wear matrix identical to STATS |
| **3** | Get/Set range helpers from table; ExtraRing **OFF**; Equip-open XY table skeleton | Low | Helpers wired; no BP33 cave |
| **4** | Full Equip-open XY consumers + pocket paint sidecar design | Low | Enter A/B; Equip open icons OK |
| **5** | ExtraRing bisect: shadows ON **without** −152 remap; rings in bag→equip one-by-one | **High** | select→enter green with −52/−53 filled |
| **6** | Delete dead Occ / draw-cave / UIToggle code paths; single avoid-list generator | None | CI/docs link stamps |

### 8.3 FIX2 staged artifact

| Item | Value |
|------|-------|
| Stamp | `ADDON_FULL_FIX2_20260802` |
| Artifact | `E:\pro\BeiDou-ijl15\ijl15.ADDON_FULL_FIX2_20260802.dll` |
| SHA256 | `F40D3BBD5471DB85F70B0B113DEF3B836A701FCC0A455E702A988F68EE25DCAB` |
| Build | `_build_full_fix2.bat` (**no auto-deploy**) |
| vs LIVE | ExtraRing OFF; Equip-open pocket; clear-only unequip; 109/red10; Occ OFF |
| Deploy gate | Separate select→enter A/B on Client_1; SHA verify; update avoid-list live section only after green |

**Do not** treat FIX2 as “architecture complete” — it is an enter-safer **delta pack** on the old ad-hoc code. Table-driven work starts after FIX2 enter-green (or consciously stays on STATS_UNEQUIP).

---

## 9. Test matrix

Run on Client_1 with live SHA recorded. **Char policy:** keep −52/−53 in bag until ExtraRing phase green.

| # | Scenario | Expect |
|---|----------|--------|
| T1 | Enter, all Addon empty | No crash; map load |
| T2 | Enter with −54/−55/−59/−60/−61 filled | No crash; stats via STAT |
| T3 | Enter with Totem×4 (−55…−58) | No crash; 5th totem refused in-game |
| T4 | Enter with pocket −33 | No `0xC0000409`; icon on **Addon row3** after Equip open |
| T5 | Enter with 109 on −10 | Vanilla shield icon; Addon aux seat empty |
| T6 | Enter with 134/135 on −10 | No crash; icon on **Addon row3** (not red10) |
| T7 | Enter with shoulder −20 + pendant2 −51 | Stats; UI per flags |
| T8 | Enter with −52/−53 (**only in ExtraRing phase**) | Must stay green; else rollback shadows |
| T9 | Unequip Addon seat | Item appears **once** in bag (no dupe); sidecar cleared |
| T10 | Unequip pocket / Si | Server bag grant; no ghost −33/−10 |
| T11 | Wear drag to Addon seats (incl. row3 pocket/aux) | Correct dst; 109 rejected on aux; no red9/10 |
| T12 | Alt-tab / char select / re-enter | Same as T1–T7 |
| T13 | Relog with Addon equipped | Sidecar re-apply; not stripped |
| T14 | Cash ring wear | Lands −112…−116 only; never −152 |
| T15 | 119 emblem | Always −59; never −10 |
| T16 | S / detail panel 四维 | Blind seats match server inject; classic seats not double-counted |

Crash triage shortcut: see avoid-list § Bisect.

---

## 10. Non-goals / forbidden patterns

Full detail: [`ADDON_ENTER_CRASH_AVOID.md`](./ADDON_ENTER_CRASH_AVOID.md).

| # | Forbidden | Failure class |
|---|-----------|---------------|
| 1 | OccShadow / Combat-MainStat Occ ON for blind seats | AV `@535351` / `@77F99E` |
| 2 | apply-max imm → 59 | BED64C / bad ZRef walk |
| 3 | Equip UIToggle / UIOpen / ctor / OnDraw detours | Enter / UI hangs |
| 4 | Widen draw-skip `bp>20 && bp<=48` or BP33 cave | `0xC0000409` GS |
| 5 | Early-return entire ExtraRing bind | Kills Addon Get/Set |
| 6 | −152/−153 cash remap in Get/Set caves | Enter-invalid / GS |
| 7 | Trust bak by name / fake DEPLOY_OK | Wrong binary live |
| 8 | Native Get/Set −52/−53 on vanilla EXE without shadows | TSec smash |
| 9 | Client optimistic bag on unequip | Dupe |
| 10 | Android/Heart on BP21/22 | Pet collision |
| 11 | Emblem/pocket via draw-skip punch | Enter GS |
| 12 | Parallel wear state machines in DLL | Desync / dupe / enter bugs |
| 13 | Copy 095 shoulder BP51 onto BeiDou −20 world | Dual numbering chaos |
| 14 | CD64 native −52…−59 until dedicated EXE program | Unproven offsets |

---

## 11. Reference index

| Path | Role |
|------|------|
| `ezorsia/extendequip/ExtendedBodyPart.h` | Phase1 C++ registry + Equip-open XY table |
| `ezorsia/shoulders/shoulders.cpp` | Flags sourced from ExtEquip; Get/Set caves |
| `ezorsia/pendant2/pendant2.cpp` | Post-field UI |
| `ezorsia/equipaddon/*` | Addon UI + sidecar + table-driven Equip-open XY |
| `gms-server/.../ExtendedEquipRegistry.java` | Phase2 Java mirror |
| `gms-server/.../EquipSlot.java` | Server slot allow-list |
| `gms-server/.../InventoryManipulator.java` | Prefix→dst via registry |
| `gms-server/.../Character.java` | Blind-slot → registry |
| `docs/ADDON_083_ADAPT_FROM_095.md` | **主路线：** 083 对照 095 适配（SENDBUSY 回滚后） |
| `docs/ADDON_ENTER_CRASH_AVOID.md` | Live SHA + avoid-list + enter-test process |
| `docs/ADDON_095_IDA_BODY_PART_NOTES.md` | 095/083 BodyPart + draw/GetSlotXY findings |
| `docs/ADDON_ROW3_POCKET_SI.md` | Pocket+aux on Addon row3; abandon red9/10 |
| `docs/ADDON_STATS_BASELINE_RECOVERY.md` | Recover STATS sources (no deploy) |
| `资料/.../095装备槽评估-20260725.md` | Full 095 bodypart survey (when IDB was mounted) |
| `资料/.../TMS120装备槽评估-20260725.md` | Higher-ver negative evidence |
| Agent transcript `1200cab1-…` | Shoulder → pendant2 → ExtraRing history (what crashed) |

---

## 12. Decision log (architecture freeze)

| Decision | Choice |
|----------|--------|
| Source of truth | Registry table (dual emit C++/Java) |
| Stats for Addon/ExtraRing | Server STAT_CHANGED; Occ OFF |
| ExtraRing on live | OFF until Phase-5 bisect |
| Pocket / Si UI | **Addon row3** overlay; one −10 storage; **no** main red9/10 |
| apply-max | Cap 55 forever on vanilla EXE |
| FIX2 / WIREOFF / P12* | Enter-unsafe or unproven — do not live |
| Phase 1–2 | Registry + server mirror; **live DLL unchanged** (STATS_UNEQUIP) |
| Enter retest | Only for new feature stamps — not STATS restores |
| Pocket icon | Addon `Paint()` row3 — never enter draw-skip punch / never main BP33 cave |
| Shield 109 | Vanilla main seat forever; Addon aux = 134/135 only |
