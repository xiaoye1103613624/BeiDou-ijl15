# IDA brief — COMPOSE + APPEND54 + TSEC (2026-08-07)

## Live

| Item | Value |
|------|--------|
| Client | `BeiDou-Client_1` |
| ijl15 SHA8 | **`AEE21638`** |
| Size / stub | 1447424 / `53 E8` |
| EXE | **`ACF10F63`** untouched |
| Stamp | `ADDON_COMPOSE_APPEND54_TSEC_20260807` |
| Bak | `ijl15.dll.bak_before_COMPOSE_APPEND54_TSEC_*` |
| Script | `tools/patch_client1_compose_append54_62_tsec_20260807.ps1` |
| Kept | EnsureLayer@34CE0 `55 8B EC 6A FF`; OriginJe@5911A `90×6` |

## IDA / evidence

| Target | Status |
|--------|--------|
| Cursor MCP IDB | **Wrong** (`v265.3.exe`) — need load `BeiDou-Client\BeiDou.exe.i64` |
| On-disk EXE `ACF10F63` | ExpectBytes OK for alias algebra |
| Prior Hex-Rays | `docs/ADDON_APPEND_ONLY_EXTEND.md` §1 |

### Bodypart alias (vanilla 52-slot)

| Write | Native ZRef | Aliases |
|-------|-------------|---------|
| BP54 | `+0xE7+54*8` = `+0x297` | cash face BP2 `+0x287+2*8` (−102) |
| BP55 | `+0xE7+55*8` = `+0x29F` | cash eye BP3 (−103) |
| BP62 | past 52-slot end | OOB / ghost |

FA `PEER_ASLR_V2` / COMPOSE `20251F3D` sidecar was **56–61 only** → badge/totem1 hit native ≡ face/eye; aux −62 never SetItem/PreferSend.

### Sites patched (file offs, ExpectBytes)

| Site | Before (FA/COMPOSE) | After |
|------|---------------------|-------|
| Apply min `@33480` | `cmp esi,56` | `cmp esi,54` |
| CashApply `@337D2` | imm `38` | **`7F` never-sidecar** |
| LoginAllowA | ≤55 + 56–62 | ≤53 + 54–62 |
| LoginAllowB | FA widen | **cash ≤51 only** |
| Get/Seh/Set/Packet/Prefer | C8/05 or 3E/06 | **CA/08 / 3E/08** (54–62) |
| ExpectedBp / aux cmp | `10` | **`62`** |
| Shoulders Apply | rings 52–55 | **52–53 only** |

## Symptom → root cause

1. **徽章双显脸饰 / 图腾1双显眼饰** — Apply/Set 未收 54/55 → 写入原生 aEquipped ≡ cash face/eye；Addon 仍可能显示 sidecar/发包残影 → 双槽。
2. **副手不显示** — SetItem/PreferSend/ExpectedBp 门仍 56–61，−62 不落 arena。
3. **新戒可再穿 + 跑帽子** — ExtraRing HT/wear 与 Origin NOP 布局已知家族；本次未改 HT 表。APPEND 收窄 shoulders 52–53，减少 54/55 误入戒影再砸原生。若复测仍帽子，调 `g_hitTestExt` XY（勿关 Origin/Layer）。

## Retest

1. Cold start → select→enter **A/B 绿**
2. StatusBar/hotkeys/minimap **不偏上**；E → 扩展栏可见
3. 徽章 → 仅 −54 / Addon，**脸饰空**
4. 图腾1 → 仅 −55，**眼饰空**
5. 134/135 → −62 有图标；109 仍 −10
6. −52/−53 tip/卸/拖 → 戒指位，**不进帽子**；六戒满后行为记录
7. 经典帽/脸/眼/武器 OK
