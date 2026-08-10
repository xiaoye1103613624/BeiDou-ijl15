# Route A — Native CharacterData grow (083 → BP≤62)

> **Date:** 2026-08-03 (status refresh 2026-08-04 ~20:55)  
> **Status:** **SUSPENDED** under user HARD RULE 2026-08-04 ~20:54「不要改原版地址」— see [`ADDON_REAL_EQUIP_EXPAND_20260804.md`](./ADDON_REAL_EQUIP_EXPAND_20260804.md) **§0 PLUGIN_ONLY**. Do **not** redeploy MINIMAL / LAYOUT_SYNC / CTOR_FIELDS EXEs.  
> **Active CD64_TEST:** vanilla EXE `ACF10F63` + plugin `FA26021C` (sidecar).  
> **Archaeology only:** this doc keeps IDA algebra for if/when user re-allows EXE grow.  
> **Parents:** [`ADDON_NATIVE_PARITY.md`](./ADDON_NATIVE_PARITY.md) · [`CharacterData扩容-进度-20260728.md`](./CharacterData扩容-进度-20260728.md) · [`ADDON_ENTER_CRASH_AVOID.md`](./ADDON_ENTER_CRASH_AVOID.md) · [`OPEN_ITEMS_20260804.md`](./OPEN_ITEMS_20260804.md)  
> **Test copy (NOT Client_1):** `BeiDou-Client_CD64_TEST`  
> **Live Client_1:** lock only — `ACF10F63` / `FA26021C`.

---

## 1. Evidence (IDA v083 `:13338` + prior Phase0/1)

| Fact | Vanilla 083 | Target (CD64) | 095 ref |
|------|-------------|----------------|---------|
| `aEquipped` / `aEquipped2` count | **52** (`push 0x34`) | **64** (`push 0x40`) | n/a (layout ≠) |
| GetItem/SetItem bound | `cmp *, -51` (`0xCD`) | `cmp *, -63` (`0xC1`) | allow **−59** |
| Normal ZRef base | `+0xE7` / `.p` `+0xEB` | **unchanged** | — |
| Cash `aEquipped2` base | `+0x287` | `+0x2E7` (+`0x60`) | — |
| Tail fields after both arrays | `≥ +0x427` | `+0xC0` | — |
| ZAlloc slab (`push` @`0x778F02`) | `0x640` | `0x700` | sizeof CD **1965** |
| Stack temp (`sub esp` @`0x46EC8F`) | `0x78C` | `0x84C` | — |
| Login apply-max (`cmp esi` @`0x4E5D0C`/`0x4E5DCC`) | **51** (`0x33`) | **62** (`0x3E`) | 58/59 UI |
| Login clear count @`0x4E5CC0` / cash @`0x4E5D82` | `push 0x34` | `push 0x40` | — |
| Cash clear lea | `[eax+0x28B]` | `[eax+0x2EB]` | — |

### Product seats (BeiDou — not blind 095)

| Seat | Item | Slot | After CD64 storage |
|------|------|-----:|--------------------|
| Extra rings | 111 | −52/−53 | native `aEquipped` |
| Badge | 118 | −54 | native |
| Totem×4 | 120 | −55…−58 | native |
| Emblem | 119 | −59 | native |
| Android | 166 | −60 | native |
| Heart | 167 | −61 | native |
| Aux weapon | 134/135 | −62 | native |
| Shield | 109 | −10 | already native |
| Pocket | 116 | −33 | already native (UI = Addon row3; **no** draw-skip punch) |
| Shoulder / pendant2 | 115/−20, 112/−51 | already native |

### Offset rule (52→64)

```
off <  0x287          → unchanged          (aEquipped + pre-cash)
0x287 ≤ off < 0x427   → off + 0x60         (old aEquipped2 body)
off ≥  0x427          → off + 0xC0         (tail after both tables)
```

Per-table insert = `12 * 8 = 0x60`. Bound −51→−63 covers product −62 + one spare.

### Why prior `BeiDou.cd64_20260728_003149.exe` entered `E_POINTER`

Patched: ctor×4, bound×5, filtered ctor-anchor imm×318, slab, stack.  
**Missed (confirmed 2026-08-03):**

1. Login clear `push 52` @`0x4E5CC0` / cash @`0x4E5D82`  
2. Cash clear displ `0x28B`→`0x2EB` (in **full** imm scan, **not** ctor-filtered set)  
3. Login apply-max left at **51** — BP52–62 never native-applied (sidecar was compensating)  
4. Plugin `kNativeCd64ExtendedSlots=false` kept Get/Set→shadow even on cd64 EXE

---

## 2. Dual-mode client policy

| EXE | Detect | Inventory truth | Addon role |
|-----|--------|-----------------|------------|
| Vanilla (`push 0x640` @`778F02`) | `DetectCd64Exe()==false` | Shadow/sidecar (legacy) | UI + login absorb |
| CD64 (`push 0x700`) | true + `kNativeCd64ExtendedSlots` | **Native ZRef −52…−62** | UI + HitTest remap only |

Detection: `*(uint8*)0x778F02 == 0x68 && *(uint32*)(0x778F03) == 0x700`.

**Forbidden on vanilla EXE:** raise apply-max past 55; native Get/Set −52…−62 without caves (TSec smash).

---

## 3. Patch sites (EXE — `tools/cd_expand/phase2_patch_copy.py`)

| Kind | VA(s) | Before → After |
|------|-------|----------------|
| ctor / dtor vector count | `0x46F534` `0x46F54C` `0x46F8E3` `0x46F8FC` | `6A 34` → `6A 40` |
| Get/Set/walk/combat bound | `0x42834E` `0x47B0BF` `0x4E5AFA` `0x72367F` `0x77F879` | imm `CD` → `C1` |
| heap slab | `0x778F02` | `push 640h` → `700h` |
| stack CD temp | `0x46EC8F` | `sub esp,78Ch` → `84Ch` |
| login clear normal | `0x4E5CC0` | `push 34h` → `40h` |
| login clear cash | `0x4E5D82` | `push 34h` → `40h` |
| cash clear lea | `0x4E5D84` | displ `28Bh` → `2EBh` |
| apply-max normal/cash | `0x4E5D0C` `0x4E5DCC` | `cmp esi,33h` → `3Eh` (62) |
| filtered field imm | `phase1_imm_sites_filtered.json` | +`0x60` / +`0xC0` |

**Never** auto-write Client_1. Output: `tools/cd_expand/out/BeiDou.cd64_YYYYMMDD_HHMMSS.exe`.

---

## 4. Plugin changes (ijl15)

When `UseNativeCd64Slots()`:

| Module | Behavior |
|--------|----------|
| `shoulders` | **No** Get/Set shadow addr caves; leave EXE bound −63; raise apply-max **62**; walk bound −63; ExtraRing draw uses **native** HasItem cave; Occ still OFF |
| `equipaddon` | **Skip** sidecar lea / jg-allow / Get-Set hooks / login clear-sidecar; Addon = paint + packet remap dst→−bp only |
| Draw / HitTest main | Cap paint **55** (Addon owns 54–62 icons); no draw-skip punch / no BP33 cave |
| OccShadow | Still **OFF** (avoid `@535351` / `@77F879`) |

Stamps: `ADDON_NATIVE_CD_YYYYMMDD` on DLL; EXE bak beside test copy.

---

## 5. Risk matrix

| Risk | Severity | Mitigation |
|------|----------|------------|
| Enter `E_POINTER` / GS (prior CD64) | **High** | Extra login clear + `0x28B` + apply-max; stage EXE only; enter matrix before Client_1 |
| Other stack-embedded CD not +`0xC0` | High | Smoke classic+cash+relog; IDA scan further `sub esp` near CD |
| Second heap pool still `0x640` | Med | `DetectCd64` false-neg → shadows; grep other `push 640h` |
| Combat bound −63 walks empty ZRefs | Low | Occ OFF; null ZRef OK |
| Blind raise apply-max on vanilla | **Forbidden** | Gate on `DetectCd64Exe` |
| DllMain Park+Get/Set+Dbg | **Forbidden** | Keep BOOTSAFE early = login caves only (vanilla path) |
| Optimistic sidecar SetItem | **Forbidden** | Packet-only wear/unequip |

---

## 6. Rollback

| Artifact | Bak / restore |
|----------|----------------|
| Live `ijl15.dll` | `ijl15.dll.bak_<stamp>` + SHA in avoid-list; proven live `6D612F01…` |
| Test CD64 EXE | Keep vanilla `BeiDou.exe`; never overwrite Client_1 without `--i-understand-live` |
| Prior bad CD64 | `tools/cd_expand/out/BeiDou.cd64_20260728_003149.exe` — do not redeploy |

---

## 7. Enter-test checklist (user — only after staged CD64 EXE + matching DLL)

1. Bak Client_1 `ijl15.dll` + record SHA; bak test `BeiDou.exe` if replacing.  
2. Boot → char select → **empty Addon** enter (T1).  
3. Classic hat/weapon + cash equip + relog.  
4. Wear −54…−62 via bag drag/dblclick (SendChange only); relog — seats match.  
5. ExtraRing −52/−53; pocket −33 on Addon row3; 109 on −10 + 134/135 on −62 together.  
6. Unequip each extended seat → item once in bag.  
7. Alt-tab / re-enter map.  
8. On any AV / `E_POINTER` / boot flash → restore vanilla EXE + live bak DLL; file dump + stamp.

---

## 8. Server

Already stores negative positions (`EquipSlot.AUX_WEAPON −62`, Android/Heart, etc.). Login migrate = empty-dest only / no cash↔normal seat swaps (`ADDON_LOGIN_KEEP_SLOTS`).

**2026-08-03 TOTEM2:** cash totems (e.g. `1202193` WZ `cash=1`) migrate to −156 on login. Client PacketSlot −156 **crashed enter** (`ADDON_NATIVE_TOTEM2` / `552212BD`). **ENTERSAFE:** keep Client_1 `6D612F01`; server `unequip` resolves −56↔−156; `equip` remaps cash totem −55…−58 → −155…−158. Restart server after Java change.

### Remaining Route A (open)

| Item | Status |
|------|--------|
| Totem2 cash −156 wear/unequip | **Server alias ENTERSAFE** (client TOTEM2 **crashed** — do not redeploy) |
| Live Client_1 | Track A (aux BP62 IDA) — **do not** overwrite from CD64 track; ENTER-RED `0F244A5D` forbid |
| CD64 test copy | **T1 GREEN** — MINIMAL `3C167A9B…` + NATIVE `8EF3A251…`; **T2–T3 open** (classic+cash / relog) |
| Full imm CD64 | **Boot-red** — filtered imm×318 → `PostGD/9F7034`. **Never dump again** |
| SAFEIMM_DISPL | **Staged** `7F90405D…` displ×241 aside — not daily EXE; Logo after T2/T3 or cash fail |
| Enter matrix | T1 green; T2–T3 user — `CD64_ENTER_MATRIX.zh-CN.md` |
| Fix imm filter | Diag + SAFEIMM builder done — [`CD64_IMM_FILTER_DIAG_20260804.md`](./CD64_IMM_FILTER_DIAG_20260804.md) |
| Sidecar OFF on CD64 | **In golden** when `DetectCd64` (slab `0x700`) |
| Promote to Client_1 | **Blocked** until T2–T3 green + prefer imm fix |
| Occ / draw-skip / BP33 | Still forbidden |
| Rebuild bat | `_build_addon_native_cd.bat` restored **STAGE ONLY** (no TOTEM2/deploy) |

---

## 9. IDA addresses (v083)

| Symbol / site | VA |
|---------------|-----|
| `CharacterData::GetItem` | `0x4282F7` (bound `0x42834E`) |
| `CharacterData::SetItem` | `0x47B05A` (bound `0x47B0BF`) |
| Login decode / apply | `0x4E592D` (clear `0x4E5CC0`, max `0x4E5D0C`, lea `0x4E5D55`, cash clear `0x4E5D82`, cash max `0x4E5DCC`, cash lea `0x4E5E18`) |
| CD ctor push 52 | `0x46F534` … |
| Heap slab | `0x778F02` |
| Stack temp | `0x46EC8F` |
| Draw-skip (do not touch) | `0x007FEE89` |
