# Client_1 — Append-only extend (2026-08-04 ~21:20)

> Prior FREEZE (~16:14) superseded by user「开始」+ append-only mandate.  
> Design: [`ADDON_APPEND_ONLY_EXTEND.md`](./ADDON_APPEND_ONLY_EXTEND.md)

## Verified live SHA (this machine)

| File | SHA256 prefix | Size | Note |
|------|---------------|-----:|------|
| `ijl15.dll` | **`60A3E54F`** | 1447424 | `ADDON_APPEND_ONLY_EXTEND_20260804` |
| `BeiDou.exe` | **`ACF10F63`** | 8503296 | **vanilla** CD (`push 0x640`) — **not** grown |
| stub `@AB30C` | `53 E8` | — | EXECAVE intact |

Rollback FA: `ijl15.dll.bak_before_APPEND_ONLY_*` → **`FA26021C`**.

## Still forbidden

- ZEROGROWTH `021C016A` / PREFER `44251097` / TOTEM1 `76C7D7FC` / PEER_COMPLETE `EADEF4A8`
- Fat MSBuild >1447424 on Client_1 (avoid #55) — CD64 artifact only
- EXE SAFEIMM / CTOR_FIELDS / cash lea reloc / mid-CD field moves

## Enter gate (user)

1. **完全退出**当前 BeiDou（内存仍可能挂着旧 FA）→ 冷启
2. select→enter **A/B 绿**
3. 红 → 立刻还原 bak FA
4. 再测：徽章/−54、图腾1/−55 不串脸/眼；134/135→−62；109→−10
