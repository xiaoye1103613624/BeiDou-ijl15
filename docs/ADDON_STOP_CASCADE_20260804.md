# STOP CASCADE — 2026-08-04 ~16:05 (AUX62_ONLY deploy)

> **2026-08-04 ~16:14 REAL EXPAND FREEZE:** Client_1 locked — no more sidecar 54–62 ZEROGROWTH / PREFER / TOTEM1 on live. Design: [`ADDON_REAL_EQUIP_EXPAND_20260804.md`](./ADDON_REAL_EQUIP_EXPAND_20260804.md). Lock: [`CLIENT_1_REAL_EQUIP_EXPAND_FREEZE.lock.md`](./CLIENT_1_REAL_EQUIP_EXPAND_FREEZE.lock.md). Work on **CD64_TEST** only.

## Live now

| Item | Value |
|------|-------|
| **ijl15** | **`3642FBDD…`** / 1447424 — `ADDON_AUX62_ONLY_20260804` (**FROZEN** — verify SHA before any deploy) |
| EXE | `ACF10F63` — **unchanged vanilla CD** |
| Base | FA26021C PEER_ASLR_V2 |
| Bak FA | `ijl15.dll.bak_before_AUX62_ONLY_20260804_*` (`FA26021C`) |
| Change | **一座一改 = 仅辅助武器**：ExpectedBp `mov eax,10→62` + curated aux `cmp …,0Ah→3Eh` |
| Untouched | PreferSend **56–61** (C8/05); totem1–4; badge −54; sidecar min; IsMainPocket sidecar C8/05 |
| Real expand | **CD64_TEST** MINIMAL `3C167A9B` + NATIVE `8EF3A251` — not this folder |

## IDA / binary cite (ijl15 FA; IDB currently BeiDou.exe — sites from live PE)

| Site | File | RVA / VA | Before → After |
|------|------|----------|----------------|
| ExpectedBp 134/135 | `0x345E3` | `0x351E3` / `100351E3` | `B8 0A 00 00 00` → `B8 3E 00 00 00` |
| WearBag aux PreferSend je | `0x377FB` | `0x383FB` / `100383FB` | `83 FE 0A` → `83 FE 3E` |
| IsMainPocketSub aux | `0x354A5` | `0x360A5` / `100360A5` | `83 F8 0A` → `83 F8 3E` |
| +22 aux cmp imm | see `tools/patch_aux62_only_fa.ps1` | — | `… 0A` → `… 3E` |
| PreferSend range | `0x37862` | `0x38462` | **UNCHANGED** `8D 46 C8 … 83 F8 05` |
| GetItem / PacketSlot / dock / AbleToWear | FA PEER | — | **already BP62** |

## Forbidden (unchanged)

`76C7D7FC` TOTEM1 / `44251097` PREFER / `021C016A` ZEROGROWTH / `EADEF4A8` / `DF3AF2F1` / `73559AC5` / `0F244A5D` / `3B864717`

## Verify now (user) — AUX ONLY

1. **Cold** kill BeiDou → start → select→enter A/B **green**
2. Dblclick/drag **134/135** → Addon row3 aux seat **−62 only** (log `bp=62`, not `bp=10`)
3. Second wear replaces first; unequip −62 clears icon (no ghost)
4. Shield **109/−10** untouched
5. **Do not** judge totem1/badge on this stamp — next 继续

If enter **red** → restore bak FA (`FA26021C`) immediately.
