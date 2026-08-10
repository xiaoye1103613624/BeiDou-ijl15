# 095 / 083 IDA — BodyPart / Equip draw / GetSlotXY notes

> **Date:** 2026-08-02 (live 095 Hex-Rays pass)  
> **Purpose:** Stop guess-and-crash flag flips for pocket(116) / Si / shield / android / heart / emblem by anchoring design on IDA facts.  
> **Deploy:** none this pass — Client_1 stays on STATS bak `4AD0AA7A…`.

---

## 1. IDA session status

| Item | Value |
|------|-------|
| MCP `user-ida-pro-mcp` (Cursor default) | Port **`:13338`** → still **v083** `BeiDou.exe` when both IDAs open |
| **095 MCP (this survey)** | Port **`:13339`** → `GMS_v95.0_U_DEVM.exe` |
| 095 IDB | `E:\资料\xiaoye\mxd学习\GMS_v95.0_U_DEVM.i64` |
| Module / imagebase | `GMS_v95.0_U_DEVM.exe` / `0x400000` |
| Hex-Rays | ready |
| `sizeof(CharacterData)` | **1965** (til named type, live `py_eval`) |
| 083 IDB (parallel) | `BeiDou-Clinet_原版未动\BeiDou.exe.i64` @ `:13338` |
| Prior written survey | `E:\资料\xiaoye\mxd学习\萧曳冒险岛\095装备槽评估-20260725.md` |

**Process note:** Opening 095 in a second IDA does **not** retarget Cursor’s `user-ida-pro-mcp` (fixed to `:13338`). Call **`:13339`** for 095, or close 083 IDA so MCP rebinds.

---

## 2. BodyPart / islot matrix (095 client vs BeiDou 083)

Convention: inventory negative slot ≈ `−BP` (cash often `−BP−100`).

### 2.1 Official GMS095 `get_bodypart_from_item` (`0x46FBE0`)

Mangled: `?get_bodypart_from_item@@YAJJJPAJH@Z` — **re-decompiled live**.

| Prefix / class | 095 BP | Notes |
|----------------|-------:|-------|
| 100…114 classic | 1–9,11–13,15–17,49–50 | Same shape as 083 |
| **109 / 119 / 134** | **10** | Shield / (095)119 / Si share BP10 |
| **135** | **11 (weapon)** | **No case 135** — falls into `prefix/10 ∈ {13,14,16,17}` → weapon. **≠ BeiDou Si−10 product** |
| 111 ring | 12(+13,15,16) | **4 rings only** — no ring5/6 |
| 112 pendant | 17(+**59**) | Pendant2 native **BP59** when `bAll` |
| **115 shoulder** | **51** | **≠ BeiDou −20 / BP20** |
| 192 | 20 | Vanilla special; BeiDou reuses BP20 for shoulder |
| 161–165 | 1100–1104 | Mechanic window |
| 194–197 | 1000–1003 | Dragon window |
| 180–183 | pet BP groups | BP14/21–48 band — **not player seats** |
| **116 pocket** | **absent** | No case |
| **118 badge / 120 totem** | **absent** | Higher-ver / private-server |
| **166 android / 167 heart** | **absent** | Zip “095开源源码” Android `≤−1200` ≠ this IDB |
| Emblem as distinct seat | **absent** | 119 → BP10 with shield in 095 |

### 2.2 `is_correct_bodypart` (`0x46F760`)

Mangled: `?is_correct_bodypart@@YAHJJJ@Z` — mirrors §2.1 allow-lists:

- 109/119/134 → only BP **10**
- 111 → only **12/13/15/16**
- 112 → **17 or 59**
- 115 → only **51**
- No AbleToWear symbol by that name; wear gating uses this + gender + item reqs elsewhere

### 2.3 BeiDou / Addon product mapping (083 — do not “align” to 095 numbers blindly)

| Product | Prefix / islot | Wear dst | UI | 095 lesson |
|---------|----------------|----------|----|------------|
| Shield | 109 `Si` | −10 | Main red10 vanilla | Same BP10 storage |
| Secondary | 134/135 `Si` | −10 | **Addon row3** (not red10) | **095: 134→10, 135→11 weapon** — keep BeiDou mutex −10; do **not** import 135→weapon; UI = Addon sidecar philosophy |
| Emblem | 119 | **−59** Addon | Addon dock | **095 maps 119→10** — BeiDou prefix-first is correct |
| Pocket | 116 `Po` | **−33** | **Addon row3** (not red9) | **Not in 095**; Draw skips BP33 on both EXEs — overlay only ([`ADDON_ROW3_POCKET_SI.md`](./ADDON_ROW3_POCKET_SI.md)) |
| Badge | 118 `Ba` | −54 | Addon | Not in 095 |
| Totem | 120 `To` | −55…−58 | Addon | Not in 095 |
| Android | 166 `Dr` | −60 | Addon | Not in 095; **never BP21** |
| Heart | 167 `Ht` | −61 | Addon | Not in 095; **never BP22** |
| Shoulder | 115 `Sh` | **−20 / BP20** | Main | Keep 083 binding; ignore 095 BP51 |
| Pendant2 | 112 | **−51 / BP51** | Main (post-field UI) | 095 uses **BP59** — different idle seat |
| ExtraRing | 111 | −52/−53 shadows | Main red3/4 | **095 has no 5th/6th ring** |

---

## 3. Equip icon draw skip (095 **live** + 083)

### 3.1 095 `CUIEquip::Draw` (`0x7AA560`)

Loop bound (same pattern as HitTest):

```c
// m_bExpanded stored oddly as v44[22].m_bScreenCoord in Hex-Rays
max = (expanded != 0) + 58;   // 58 unexpanded / 59 expanded
for (pRecta = 0; …; ) {
  v15 = nSlotIndex = pRecta + 1;          // bodypart 1-based
  if (pRecta == 13 || (v15 > 20 && v15 <= 48))
    goto skip;                            // BP14 + BP21–48
  // … DrawItemIconForSlot via m_sEqSlotInfo …
  // BP59 (pRecta==58): special expire check vs aEquipped2
}
```

| Fact | Implication |
|------|-------------|
| Skip band **identical** to 083 (`14` + `21…48`) | **BP33 is skipped on official 095 too** |
| Loop extends to **58/59** | Shoulder BP51 + pendant2 BP59 can paint **because they sit outside 21–48** |
| Pocket has no native BP | No “095 pocket paint” path; punching skip still wrong on both EXEs |
| Job kits | Separate `CUIMechanicEquip::Draw` / `CUIDragonEquip::Draw` — not jammed into main `[50]` |

### 3.2 083 (verified earlier on `:13338`)

Function: `sub_7FEC81`. Asm **`0x007FEE89`**: `cmp …, 30h` (=48), `jle` skip.

Punching `48→32` / BP33 cave → enter `0xC0000409` — avoid-list.

---

## 4. GetSlotXY / HitTest

### 4.1 095 HitTest — `CUIEquip::GetBodyPartFromPoint` (`0x7A3CE0`)

```c
v5 = (this->m_ewi.m_bExpanded != 0) + 58;  // 58 or 59
for (i = m_sEqSlotInfo; v4 < v5; ++i, ++v4)
  if (hit 32×32 cell) return v4 + 1;       // bodypart
```

| Symbol | VA (095) | Note |
|--------|----------|------|
| `CUIEquip::GetBodyPartFromPoint` | `0x7A3CE0` | Unexpanded **58** / expanded **59** |
| `m_sEqSlotInfo` | `0xC6E858` | `CExpandableWndInfo` runtime table (`EqSlotInfo` size 8) |
| `sEqSlotInfo` | `0xC61458` | Static BP1–50 coords (e.g. BP1=(38,35), BP10=(137,134)) |
| Slot init | `0xB0D7C0` | Fills `m_sEqSlotInfo` via `EqSlotInfo::GetX/GetY` |
| `EqSlotInfo::GetX` | `0x7A3910` | Grid; `nXpt<0` → **1024** (off-screen pet pad) |
| `EqSlotInfo::GetY` | `0x7A3960` | Grid; `nYpt<0` → **768** |
| `CUIMechanicEquip::GetBodyPartFromPoint` | `0x7A3870` | 5 cells → **1100+i** |
| `CUIDragonEquip::GetBodyPartFromPoint` | `0x7A38C0` | 4 cells → **1000+i** |

**No** `CUIEquip::GetSlotXY` symbol on 095 (083 has `0x007FEFEA` table index helper).

### 4.2 083 HitTest / GetSlotXY (unchanged)

| Symbol | VA | Behavior |
|--------|-----|----------|
| `CUIEquip::GetSlotXY` | `0x007FEFEA` | `8*index + (cash ? 0xBE23F0 : 0xBE2260)` — **no bounds check** |
| `GetBodyPartFromPoint` | `0x007FEC32` | Scans `g_aEquipSlotPos` … skips index **13** and **20…47** |
| Coord table | `g_aEquipSlotPos[50]` | Hard cap **50** ACTIVE main seats |

**Safe recipe (both versions):** park/remap XY in cells that already exist, or DLL-owned HitTest buffer; never write past vanilla `[50]` / apply-max without full consumer audit. Gate wire to **Equip UI live**.

---

## 5. Inventory encode / equip slot count (095)

| API | VA | Rule |
|-----|-----|------|
| `CharacterData::GetItem` | `0x42B990` | Equip TI=1: reject gap `[-100,-60]`; allow `aEquipped[-nPos]` for **`nPos ∈ [-59,-1]`**; cash `[-159,-101]`; Dragon `−1000…−1003`; Mechanic `−1100…−1104` |
| `CharacterData::SetItem` | `0x4946C0` | Same −59 / −159 / 1000+ / 1100+ windows |
| `sizeof(CharacterData)` | til | **1965** bytes — structure grown so −52…−59 can be **native** |
| Draw uses | `aEquipped` + `aEquipped2` | Dual arrays for expire / cash dual-look |

**083 constraint:** vanilla apply-max **≤55**; hard-writing past that smashes TSec. **Cannot** copy 095’s −59 Get/Set range onto stock BeiDou.exe without a CD64 / structure-grow project. ExtraRing −52/−53 and Addon −54…−61 stay **shadow / sidecar**.

---

## 6. Shield vs secondary (Si)

| Item | 095 | BeiDou 083 product |
|------|-----|--------------------|
| 109 shield | BP10 | −10, native draw |
| 119 | BP10 (same as shield!) | Emblem **−59** — **do not copy 095** |
| 134 | BP10 | −10 Si / red10 XY |
| 135 | **BP11 weapon** | −10 Si mutex with 109/134 |

UI split red10 on 083 is **coordinate-only**; storage stays one Si −10 so AvatarLook / encode / two-hand unequip stay correct.

---

## 7. What v083 can reuse vs must sidecar

### Copy / inspire (architecture only)

1. **Grow HitTest/Draw max with real seats** (095 → 58/59) — *pattern*, not addresses.
2. **Job kits = separate windows** (Mechanic 1100+ / Dragon 1000+) — never overflow `g_aEquipSlotPos[50]`.
3. **Pendant2 / shoulder as real BP outside pet skip** — 095 paints 51/59 because they are **not** in 21–48.
4. **Prefix→BP table + `is_correct_bodypart`** as server/client allow-list model.
5. **CharacterData must grow** before native −56…−59 Get/Set — 095’s 1965-byte CD is the proof, not a patch target.

### Do **not** copy

| 095 fact | Why forbidden on 083 |
|----------|----------------------|
| 115→BP51 | BeiDou already shipped **BP20/−20** |
| 112 second seat BP59 | BeiDou pendant2 idle **BP51/−51**; emblem owns −59 |
| 119→BP10 | Would smash shield/Si |
| 135→weapon | Breaks Si mutex product |
| Draw skip unchanged | Still skips BP33 — **no pocket paint gift** |
| GetItem −59 native | 083 apply-max 55 / smaller CD |
| Zip server Android ≤−1200 | ≠ this IDB; wrong protocol |

### Must sidecar on 083

Pocket 116, badge 118, totem 120, android 166, heart 167, ExtraRing 5/6, emblem −59 — Addon dock + Equip-open XY / shadow Get-Set; **never** enter-time draw-skip mutation.

---

## 8. Key addresses (095 quick index)

| Symbol | VA |
|--------|-----|
| `get_bodypart_from_item` | `0x46FBE0` |
| `is_correct_bodypart` | `0x46F760` |
| `get_gender_from_id` | `0x46F6D0` |
| `CUIEquip::GetBodyPartFromPoint` | `0x7A3CE0` |
| `CUIEquip::Draw` | `0x7AA560` |
| `EqSlotInfo::GetX` / `GetY` | `0x7A3910` / `0x7A3960` |
| `CUIMechanicEquip::GetBodyPartFromPoint` | `0x7A3870` |
| `CUIDragonEquip::GetBodyPartFromPoint` | `0x7A38C0` |
| `CharacterData::GetItem` / `SetItem` | `0x42B990` / `0x4946C0` |
| `CharacterData::Decode` | `0x4FCCE0` |
| `g_anRingBodyPart` | `0xC56240` → `{12,13,15,16,22…}` (tail = pet overlap) |
| `sEqSlotInfo` / `m_sEqSlotInfo` | `0xC61458` / `0xC6E858` |
| Slot-name strings | `0xB4C718`… (`shoulder` @ `0xB4C734`; no `ring5`/`pocket`) |

083 counterparts: draw-skip `0x007FEE89`, GetSlotXY `0x007FEFEA`, HitTest `0x007FEC32`, AbleToWear `0x4F2CEE`.

---

## 9. Design takeaways

1. **095 is the right research tool** for “how native extended seats grow” — not a drop-in pocket/android/totem/ExtraRing kit.
2. Extended seats that paint on 095 are **real BPs outside the pet skip band** + **expanded HitTest (58/59)** + **larger CharacterData (1965 / −59)**.
3. Pocket BP33 is in the skip band on **both** 083 and 095 → **Addon row3 overlay** (not main red9 / not draw-skip).
4. Si/shield: keep **one storage −10**; 109 = main vanilla; 134/135 = Addon row3; ignore 095’s 119→10 and 135→weapon.
5. UI philosophy to copy: Mechanic/Dragon **separate windows** — BeiDou Addon dock is that window.
6. Next coding step: **STATS baseline recovery** → then Addon row3 seats ([`ADDON_ROW3_POCKET_SI.md`](./ADDON_ROW3_POCKET_SI.md)); **no Client_1 deploy**, no enter test until intentional feature stamp.

---

## 10. Reproduce

1. Start 095 IDA on `GMS_v95.0_U_DEVM.i64` (MCP listens `:13339` if 083 already holds `:13338`).
2. `server_health` → module must be `GMS_v95.0_U_DEVM.exe`.
3. Jump `0x46FBE0`, `0x46F760`, `0x7A3CE0`, `0x7AA560`, `0x42B990`.
4. Strings: `shoulder` yes; `ring5` / `pocket` no.
5. `py_eval` `sizeof(CharacterData)` → 1965.

---

*Linked from [`ADDON_EXTEND_EQUIP_ARCHITECTURE.md`](./ADDON_EXTEND_EQUIP_ARCHITECTURE.md) §7.*
