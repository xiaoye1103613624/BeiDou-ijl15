# Addon seats — all-or-nothing + level/job (2026-08-04) — CASCADE postmortem

## Product rules

1. **All-or-nothing:** Totem×4, emblem, android, heart, badge, pocket, **aux −62** share **one** pipeline (AbleToWear / SendChange / **sidecar Get/Set**). Incomplete peer wiring ≠ seat-specific omit.
2. **Level + job:** Extended seats pass vanilla `reqLevel` / `reqJob`. Do not bypass for Addon.
3. **No vanilla Equip tip/draw edits** beyond park-off-panel — Addon/sidecar mapping only.
4. **Sidecar seats −54…−62:** ALWAYS `EquipAddon` sidecar ZRef — **NEVER** native `aEquipped[]` (52 slots).

## Why problems cascaded (honest)

Vanilla CharData `aEquipped` is **52** ZRefs (`push 34h` @`0x4E5CC0`). Cash array starts exactly **52×8** later (`lea +0x287` @`4E5E18` vs `+0xE7` @`4E5D55`).

| Native write (esi=BP) | Same cell as (IDA) | UI seat |
|----------------------|--------------------|---------|
| BP54 | cash BP2 (−102) **脸饰** | badge 118 |
| BP55 | cash BP3 (−103) **眼饰** | totem1 120 |
| BP62 | past cash → OOB / multi-ghost | aux 134/135 |

User 2026-08-04 report (totem→脸 / badge→眼) is **face↔eye vs table** — IDA math above is authoritative; both 54+55 must leave native.

`PEER_COMPLETE` (`EADEF4A8`) remapped aux imm 10→62 and cleared omit, but left **ApplyEquip redirect at 56–62** and **wear_fn** for 54/55. So badge/totem still hit native/wear paths → face/eye ghosts; aux wear_fn wrote OOB while occupied-check read empty sidecar → multi-wear illusion. Incomplete sidecar = new seats collide with cash. FA `PEER_ASLR_V2` kept 56–61(+62) only → same 54/55 ghosts.

## Fix A2 (REGRESSION — rolled back ~15:30) — stamp `ADDON_SIDECAR_PREFER_AUX62_20260804`

Zero-growth on ZEROGROWTH (`tools/patch_sidecar_prefersend_aux62.ps1`): PreferSend **54–62** + ExpectedBp/IsMainPocket aux **10→62**. Intended to close ghost unequip + aux bp=10. **Live result:** blank totems/badge, endless wear, aux unequip ghost. **Aside `44251097` — forbid.** Live restored to **FA26021C**. See [`ADDON_STOP_CASCADE_20260804.md`](./ADDON_STOP_CASCADE_20260804.md).

## Fix A (prior) — stamp `ADDON_SIDECAR_54_62_ZEROGROWTH_20260804`

Zero-growth binary patch on FA (`tools/patch_sidecar_54_62_zerogrowth.ps1`):

| Change | Detail |
|--------|--------|
| ForBp / IsSidecar idiom | 56–61 → **54–61** (bp62 early-exit kept) |
| ApplyEquip / CashApply | min **56→54** (max 62) |
| LoginAllow A/B | native ≤**53**; sidecar **54–62** |
| GetItem/SehGet/SetItem/PacketSlot | normal **−62…−54**; cash GetItem NOP path untouched |
| Shoulders ApplyEquip | rings **52–53 only** |
| ClearShadows | walk start **54** |
| PreferSendChangeOnly | **not** patched (still FA wear_fn path) — left badge/totem ghosts; fixed by A2 |

## Fix B (growth golden) — stamp `ADDON_SIDECAR_54_62_20260804`

| Change | Detail |
|--------|--------|
| ApplyEquip / CashApply | `cmp esi,54` … `62` → sidecar |
| PreferSendChangeOnly | true for pocket+sidecar — **no wear_fn** |
| Shoulders ApplyEquip | rings **52–53 only** |
| ShadowZRefForBp(54/55) | → `EquipAddon_SidecarZRefForBp` |
| Server aux | strip stray 134/135 off −10/−110; replace-at−62 unchanged |

## Artifacts

| Item | Value |
|------|-------|
| Live (STOP CASCADE) | **`FA26021C…`** / **1447424** PEER_ASLR_V2 |
| Aside REGRESSION | `ijl15.dll.aside_REGRESSION_PREFER_AUX62_44251097_*` — **FORBIDDEN** |
| Forbid ZEROGROWTH | `021C016A…` (incomplete PreferSend cascade) |
| Aside COMPLETE | `ijl15.dll.aside_PEER_COMPLETE_CASCADE_*` (`EADEF4A8`) |
| Growth golden (not live) | `golden\ijl15.ADDON_SIDECAR_54_62_20260804.dll` `32C55435…` / **1520640** |
| Zero-growth golden | `golden\ijl15.ADDON_SIDECAR_54_62_ZEROGROWTH_20260804.dll` `021C016A…` |
| Server | `GREEN_ENTER_OMIT_AUX62=false` + aux stray−10 strip |

## Retest matrix (after enter-green)

1. Cold select→enter A/B **green**（先于功能测）
2. Aux 134/135 双击 → **仅** Addon −62；再穿替换，不占盾 −10
3. Badge 118 → Addon −54 only；**点装脸饰不显示**
4. Totem1 120 → Addon −55 only；**点装眼饰不显示**
5. Level/job 拒绝在 Addon 座仍生效

## Forbidden

- Redeploy `EADEF4A8` COMPLETE / ENTER-RED `DF3AF2F1` / `73559AC5` / `0F244A5D`
- Live-promote growth `32C55435` before enter A/B (prefer ZEROGROWTH)
- Re-enable wear_fn for BP54–62 / native aEquipped[54+]
- Touch vanilla Equip tip (`sub_7FE9BB` / dual-tip) for this fix
- On enter red: restore `FA26021C` bak immediately
